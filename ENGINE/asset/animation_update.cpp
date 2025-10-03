#include "animation_update.hpp"
#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"
#include "asset/asset_types.hpp"
#include "animation.hpp"
#include "core/AssetsManager.hpp"
#include "core/asset_list.hpp"
#include "audio/audio_engine.hpp"
#include "utils/area.hpp"
#include "utils/range_util.hpp"
#include <SDL.h>
#include <limits>
#include <cmath>
#include <algorithm>
#include <random>
#include <string>
#include <vector>
#include <iostream>
#include <unordered_map>

namespace {
inline void normalize_minmax(int& mn, int& mx) {
    if (mx < mn) std::swap(mn, mx);
    mn = std::max(0, mn);
    mx = std::max(0, mx);
}
inline double rand_angle(std::mt19937& rng) {
    std::uniform_real_distribution<double> dist(0.0, 2.0 * 3.14159265358979323846);
    return dist(rng);
}
inline double rand_real(std::mt19937& rng, double lo, double hi) {
    std::uniform_real_distribution<double> dist(lo, hi);
    return dist(rng);
}
inline int rand_int(std::mt19937& rng, int lo, int hi) {
    std::uniform_int_distribution<int> dist(lo, hi);
    return dist(rng);
}
inline double angle_from_or_random(int vx, int vy, std::mt19937& rng) {
    if (vx == 0 && vy == 0) return rand_angle(rng);
    return std::atan2(static_cast<double>(vy), static_cast<double>(vx));
}

struct ManualState {
    int manual_dx = 0;
    int manual_dy = 0;
    int last_dir_x = 0;
    int last_dir_y = 1;
    std::string last_anim = "default";
    bool last_was_moving = false;
    bool active = false;
};

ManualState& manual_state(AnimationUpdate* updater) {
    static std::unordered_map<AnimationUpdate*, ManualState> states;
    return states[updater];
}

// No extraction helpers: iterate AssetList sections directly where needed.
}

AnimationUpdate::AnimationUpdate(Asset* self, Assets* assets)
: self_(self), assets_owner_(assets) {
    if (!assets_owner_ && self_) {
        assets_owner_ = self_->get_assets();
    }
    std::seed_seq seed{
        static_cast<unsigned>(reinterpret_cast<uintptr_t>(self) & 0xffffffffu), static_cast<unsigned>((reinterpret_cast<uintptr_t>(self) >> 32) & 0xffffffffu) };
    rng_.seed(seed);
    path_bias_ = 0.75;
    int def_max = 100;
    if (self_ && self_->get_neighbors_list()) {
        def_max = std::max(1, self_->get_neighbors_list()->search_radius());
    }
    max_current_target_dist = def_max;
    min_current_target_dist = std::max(1, static_cast<int>(std::floor(min_factor * def_max)));
}

AnimationUpdate::AnimationUpdate(Asset* self, Assets* assets,
                                 double path_bias)
: self_(self), assets_owner_(assets) {
    path_bias_ = std::clamp(path_bias, 0.0, 1.0);
    std::seed_seq seed{
        static_cast<unsigned>(reinterpret_cast<uintptr_t>(self) & 0xffffffffu), static_cast<unsigned>((reinterpret_cast<uintptr_t>(self) >> 32) & 0xffffffffu) };
    rng_.seed(seed);
    if (!assets_owner_ && self_) {
        assets_owner_ = self_->get_assets();
    }
    int def_max = 100;
    if (self_ && self_->get_neighbors_list()) {
        def_max = std::max(1, self_->get_neighbors_list()->search_radius());
    }
    max_current_target_dist = def_max;
    min_current_target_dist = std::max(1, static_cast<int>(std::floor(min_factor * def_max)));
}

void AnimationUpdate::transition_mode(Mode m) {
    if (mode_ == m) return;
    mode_ = m;
    have_target_ = false;
    if (m != Mode::Orbit)      orbit_params_set_ = false;
    if (m != Mode::Serpentine) serp_params_set_  = false;
    if (m != Mode::Patrol)     patrol_initialized_ = false;
    if (m != Mode::ToPoint)    to_point_on_reach_ = nullptr;
}

int AnimationUpdate::min_move_len2() const {
    if (cached_min_move_len2_ >= 0) return cached_min_move_len2_;
    cached_min_move_len2_ = std::numeric_limits<int>::max();
    if (!self_ || !self_->info) { cached_min_move_len2_ = 1; return cached_min_move_len2_; }
    for (const auto& kv : self_->info->animations) {
        const Animation& anim = kv.second;
        const int dx = anim.total_dx, dy = anim.total_dy;
        if (dx == 0 && dy == 0) continue;
        const int len2 = dx*dx + dy*dy;
        if (len2 > 0) cached_min_move_len2_ = std::min(cached_min_move_len2_, len2);
    }
    if (cached_min_move_len2_ == std::numeric_limits<int>::max()) cached_min_move_len2_ = 1;
    return cached_min_move_len2_;
}

bool AnimationUpdate::is_target_reached() {
    if (!self_) return true;
    const SDL_Point here = self_->pos;
    const double d = Range::get_distance(here, target_);
    const double step = std::sqrt(static_cast<double>(min_move_len2()));
    return d <= step;
}

void AnimationUpdate::set_target(SDL_Point desired, const Asset* final_target) {
    if (!self_) return;

    normalize_minmax(min_current_target_dist, max_current_target_dist);

    const SDL_Point origin = self_->pos;
    const SDL_Point aim    = final_target ? final_target->pos : desired;

    double dir_x = static_cast<double>(desired.x - origin.x);
    double dir_y = static_cast<double>(desired.y - origin.y);
    double dir_len = std::hypot(dir_x, dir_y);
    if (dir_len < 1e-6) {
        const double angle = rand_angle(rng_);
        dir_x = std::cos(angle);
        dir_y = std::sin(angle);
        dir_len = 1.0;
    }

    const double min_step = static_cast<double>(min_current_target_dist);
    const double max_step = static_cast<double>(max_current_target_dist);
    const double desired_step = std::clamp(dir_len, min_step, max_step);
    const double bias = std::clamp(path_bias_, 0.0, 1.0);

    struct Vec2 { double x; double y; };
    const Vec2 forward{ dir_x / dir_len, dir_y / dir_len };
    const Vec2 lateral{ -forward.y, forward.x };

    auto make_point = [&](double forward_weight, double lateral_weight) -> SDL_Point {
        double vx = forward.x * forward_weight + lateral.x * lateral_weight;
        double vy = forward.y * forward_weight + lateral.y * lateral_weight;
        double vlen = std::hypot(vx, vy);
        if (vlen < 1e-6) {
            vx = forward.x;
            vy = forward.y;
            vlen = 1.0;
        }
        const double scale = desired_step / vlen;
        return SDL_Point{
            origin.x + static_cast<int>(std::llround(vx * scale)),
            origin.y + static_cast<int>(std::llround(vy * scale))
        };
    };

    struct Candidate { SDL_Point point; double cost; };
    std::vector<Candidate> candidates;
    candidates.reserve(8);

    auto add_candidate = [&](double fw, double lw, double penalty) {
        SDL_Point pt = make_point(fw, lw);
        double cost = Range::get_distance(pt, aim) + penalty * (1.0 - bias) * desired_step;
        candidates.push_back(Candidate{ pt, cost });
    };

    add_candidate(1.0, 0.0, 0.0);
    add_candidate(0.9,  0.35, 1.0);
    add_candidate(0.9, -0.35, 1.0);
    add_candidate(0.75,  0.65, 2.0);
    add_candidate(0.75, -0.65, 2.0);
    add_candidate(0.4,   1.0,  3.0);
    add_candidate(0.4,  -1.0,  3.0);

    double final_dx = static_cast<double>(aim.x - origin.x);
    double final_dy = static_cast<double>(aim.y - origin.y);
    double final_len = std::hypot(final_dx, final_dy);
    if (final_len >= 1e-6) {
        const double clamp_len = std::clamp(final_len, min_step, max_step);
        const double scale = clamp_len / final_len;
        SDL_Point direct_goal{
            origin.x + static_cast<int>(std::llround(final_dx * scale)),
            origin.y + static_cast<int>(std::llround(final_dy * scale))
        };
        double cost = Range::get_distance(direct_goal, aim);
        candidates.push_back(Candidate{ direct_goal, cost });
    }

    const double random_angle = rand_angle(rng_);
    SDL_Point random_pt{
        origin.x + static_cast<int>(std::llround(desired_step * std::cos(random_angle))),
        origin.y + static_cast<int>(std::llround(desired_step * std::sin(random_angle)))
    };
    double random_cost = Range::get_distance(random_pt, aim) + 4.0 * (1.0 - bias) * desired_step;
    candidates.push_back(Candidate{ random_pt, random_cost });

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.cost == b.cost) {
            if (a.point.x == b.point.x) return a.point.y < b.point.y;
            return a.point.x < b.point.x;
        }
        return a.cost < b.cost;
    });
    candidates.erase(std::unique(candidates.begin(), candidates.end(), [](const Candidate& lhs, const Candidate& rhs) {
        return lhs.point.x == rhs.point.x && lhs.point.y == rhs.point.y;
    }), candidates.end());

    const SDL_Point bottom_origin = bottom_middle(origin);
    for (const Candidate& c : candidates) {
        const SDL_Point bottom_candidate = bottom_middle(c.point);
        if (point_in_impassable(bottom_candidate, final_target)) continue;
        if (path_blocked(bottom_origin, bottom_candidate, final_target)) continue;
        target_ = c.point;
        have_target_ = true;
        moving = (Range::get_distance(origin, target_) > 1.0);
        return;
    }

    if (!candidates.empty()) {
        target_ = candidates.front().point;
        have_target_ = true;
        moving = (Range::get_distance(origin, target_) > 1.0);
        return;
    }

    target_ = origin;
    have_target_ = false;
    moving = false;
}

SDL_Point AnimationUpdate::bottom_middle(SDL_Point pos) const {
    if (!self_ || !self_->info) return pos;
    return SDL_Point{ pos.x, pos.y - self_->info->z_threshold };
}

bool AnimationUpdate::point_in_impassable(SDL_Point pt, const Asset* ignored) const {
    Asset* closest = nullptr;
    double best_d2 = std::numeric_limits<double>::infinity();

    const AssetList* impassable = self_ ? self_->get_impassable_naighbors() : nullptr;
    auto consider = [&](Asset* a) {
        if (!a || a == self_ || a == ignored || !a->info) return;
        if (a->info->type == asset_types::texture) return;
        if (a->info->passable) return;
        const double dx = static_cast<double>(a->pos.x - pt.x);
        const double dy = static_cast<double>(a->pos.y - pt.y);
        const double d2 = dx * dx + dy * dy;
        if (d2 < best_d2) {
            best_d2 = d2;
            closest = a;
        }
    };

    if (impassable) {
        for (Asset* a : impassable->top_unsorted()) consider(a);
        for (Asset* a : impassable->middle_sorted()) consider(a);
        for (Asset* a : impassable->bottom_unsorted()) consider(a);
    } else if (assets_owner_) {
        const auto& active = assets_owner_->getActive();
        for (Asset* a : active) {
            consider(a);
        }
    }

    if (!closest) return false;

    static const char* kNames[] = { "impassable_area", "passability", "collision_area" };
    for (const char* nm : kNames) {
        Area obstacle = closest->get_area(nm);
        if (obstacle.get_points().size() >= 3 && obstacle.contains_point(pt)) {
            return true;
        }
    }
    return false;
}

bool AnimationUpdate::path_blocked(SDL_Point from, SDL_Point to, const Asset* ignored) const {
    if (from.x == to.x && from.y == to.y) {
        return point_in_impassable(to, ignored);
    }
    const double dist = Range::get_distance(from, to);
    const double step_len = std::max(1.0, std::sqrt(static_cast<double>(min_move_len2())));
    const int steps = std::max(1, static_cast<int>(std::ceil(dist / step_len)));
    for (int i = 1; i <= steps; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(steps);
        const int sx = static_cast<int>(std::lround(from.x + (to.x - from.x) * t));
        const int sy = static_cast<int>(std::lround(from.y + (to.y - from.y) * t));
        if (point_in_impassable(SDL_Point{ sx, sy }, ignored)) {
            return true;
        }
    }
    return false;
}

void AnimationUpdate::set_path_bias(double bias) {
    path_bias_ = std::clamp(bias, 0.0, 1.0);
}

void AnimationUpdate::set_idle(int rest_ratio) {
    if (mode_ == Mode::Idle) return;
    idle_rest_ratio_ = std::clamp(rest_ratio, 0, 100);
    transition_mode(Mode::Idle);
}

void AnimationUpdate::set_pursue(Asset* final_target) {
    if (mode_ == Mode::Pursue) return;
    pursue_target_   = final_target;
    transition_mode(Mode::Pursue);
}

void AnimationUpdate::set_run(Asset* threat) {
    if (mode_ == Mode::Run) return;
    run_threat_   = threat;
    transition_mode(Mode::Run);
}

void AnimationUpdate::set_orbit(Asset* center, int min_radius, int max_radius, int keep_direction_ratio) {
    if (mode_ == Mode::Orbit) return;
    orbit_center_    = center;
    orbit_min_radius_= min_radius;
    orbit_max_radius_= max_radius;
    orbit_keep_ratio_= keep_direction_ratio;
    orbit_force_dir_ = false;
    transition_mode(Mode::Orbit);
}

void AnimationUpdate::set_patrol(const std::vector<SDL_Point>& waypoints, bool loop, int hold_frames) {
    if (mode_ == Mode::Patrol) return;
    patrol_points_       = waypoints;
    patrol_loop_         = loop;
    patrol_hold_frames_  = std::max(0, hold_frames);
    patrol_initialized_  = false;
    transition_mode(Mode::Patrol);
}

void AnimationUpdate::set_serpentine(Asset* final_target, int sway, int keep_side_ratio) {
    if (mode_ == Mode::Serpentine) return;
    serp_target_      = final_target;
    serp_sway_        = sway;
    serp_keep_ratio_  = keep_side_ratio;
    transition_mode(Mode::Serpentine);
}

void AnimationUpdate::set_to_point(SDL_Point final_point, std::function<void(AnimationUpdate&)> on_reached) {
    to_point_goal_    = final_point;
    to_point_on_reach_ = std::move(on_reached);
    transition_mode(Mode::ToPoint);
}

void AnimationUpdate::set_mode_none() {
    transition_mode(Mode::None);
}

bool AnimationUpdate::can_move_by(int dx, int dy) const {
    if (!self_ || !self_->info) return false;
    SDL_Point next{ self_->pos.x + dx, self_->pos.y + dy };
    SDL_Point bottom = bottom_middle(next);
    return !point_in_impassable(bottom, nullptr);
}

bool AnimationUpdate::would_overlap_same_or_player(int dx, int dy) const {
    if (!self_ || !self_->info) return true;
    const SDL_Point new_pos{ self_->pos.x + dx, self_->pos.y + dy };
    const AssetList* neighbor_list = self_->get_neighbors_list();
    if (neighbor_list) {
        auto check = [&](Asset* a) {
            if (!a || a == self_ || !a->info) return false;
            const bool is_enemy  = (a->info->type == asset_types::enemy);
            const bool is_player = (a->info->type == asset_types::player);
            if (!is_enemy && !is_player) return false;
            return Range::get_distance(new_pos, a) < 40.0;
        };
        for (Asset* a : neighbor_list->top_unsorted()) { if (check(a)) return true; }
        for (Asset* a : neighbor_list->middle_sorted()) { if (check(a)) return true; }
        for (Asset* a : neighbor_list->bottom_unsorted()) { if (check(a)) return true; }
    } else if (assets_owner_) {
        const auto& active = assets_owner_->getActive();
        for (Asset* a : active) {
            if (!a || a == self_ || !a->info) continue;
            const bool is_enemy  = (a->info->type == asset_types::enemy);
            const bool is_player = (a->info->type == asset_types::player);
            if (!is_enemy && !is_player) continue;
            if (Range::get_distance(new_pos, a) < 40.0) return true;
        }
    }
    return false;
}

std::string AnimationUpdate::pick_best_animation_towards(SDL_Point target) {
    if (!self_ || !self_->info) return {};
    const auto& all = self_->info->animations;
    if (all.empty()) return {};
    const int cx = self_->pos.x;
    const int cy = self_->pos.y;
    double best_d = std::numeric_limits<double>::infinity();
    std::string best_id;
    for (const auto& kv : all) {
        const std::string& id = kv.first;
        const Animation& anim = kv.second;
        if (anim.number_of_frames <= 0 || anim.frames_data.empty()) continue;
        if (!anim.frames.empty() && anim.frames_data.size() != anim.frames.size()) continue;
        const int dx = anim.total_dx;
        const int dy = anim.total_dy;

        if (moving) {
            if (dx == 0 && dy == 0) continue;
        } else {
            if (dx != 0 || dy != 0) continue;
        }
        if (!can_move_by(dx, dy)) continue;
        if (would_overlap_same_or_player(dx, dy)) continue;
        const SDL_Point next{ cx + dx, cy + dy };
        const double d = Range::get_distance(next, target);
        if (d < best_d) {
            best_d = d;
            best_id = id;
        }
    }
    if (best_id.empty()) {
        get_new_target();
    }
    return best_id;
}

void AnimationUpdate::ensure_idle_target() {
    if (!self_) return;
    normalize_minmax(min_current_target_dist, max_current_target_dist);
    const int rest_pct = std::clamp(idle_rest_ratio_, 0, 100);
    const double roll = rand_real(rng_, 0.0, 100.0);
    if (roll < static_cast<double>(rest_pct)) {
        have_target_ = false;
        moving = false;
        return;
    }
    const int cx = self_->pos.x;
    const int cy = self_->pos.y;
    const double a = rand_angle(rng_);
    const double r = rand_real(rng_, static_cast<double>(min_current_target_dist), static_cast<double>(max_current_target_dist));
    const int tx = cx + static_cast<int>(std::llround(r * std::cos(a)));
    const int ty = cy + static_cast<int>(std::llround(r * std::sin(a)));
    moving = true;
    set_target(SDL_Point{ tx, ty }, nullptr);
}




void AnimationUpdate::ensure_pursue_target( const Asset* final_target) {
    if (!self_ || !final_target) return;
    normalize_minmax(min_current_target_dist, max_current_target_dist);
    const int cx = self_->pos.x, cy = self_->pos.y;
    const int tx = final_target->pos.x, ty = final_target->pos.y;
    const double a = angle_from_or_random(tx - cx, ty - cy, rng_);
    const double r = rand_real(rng_, static_cast<double>(min_current_target_dist), static_cast<double>(max_current_target_dist));
    int nx = cx + static_cast<int>(std::llround(r * std::cos(a)));
    int ny = cy + static_cast<int>(std::llround(r * std::sin(a)));
    set_target(SDL_Point{nx, ny}, final_target);
}

void AnimationUpdate::ensure_run_target( const Asset* threat) {
    if (!self_ || !threat) return;
    normalize_minmax(min_current_target_dist, max_current_target_dist);
    const int cx = self_->pos.x, cy = self_->pos.y;
    const int tx = threat->pos.x, ty = threat->pos.y;
    const double a = angle_from_or_random(cx - tx, cy - ty, rng_);
    const double r = rand_real(rng_, static_cast<double>(min_current_target_dist), static_cast<double>(max_current_target_dist));
    int nx = cx + static_cast<int>(std::llround(r * std::cos(a)));
    int ny = cy + static_cast<int>(std::llround(r * std::sin(a)));
    set_target(SDL_Point{nx, ny}, nullptr);
}

void AnimationUpdate::ensure_orbit_target(int min_radius, int max_radius, const Asset* center, int keep_direction_ratio) {
    
    //this needs to handle min/min_current_target_dist for setting current target point that lies on the radius
    
    if (!self_ || !center) return;
    normalize_minmax(min_radius, max_radius);
    if (orbit_params_set_) {
        if (!orbit_force_dir_) {
            const int denom = std::max(0, keep_direction_ratio) + 1;
            if (rand_int(rng_, 0, denom - 1) == 0) orbit_dir_ = -orbit_dir_;
        } else {
            orbit_dir_ = (orbit_forced_dir_ >= 0) ? +1 : -1;
        }
    } else {
        orbit_dir_ = orbit_force_dir_ ? ((orbit_forced_dir_ >= 0) ? +1 : -1) : (rand_int(rng_, 0, 1) ? +1 : -1);
    }
    if (!orbit_params_set_) {
        orbit_radius_ = rand_int(rng_, min_radius, max_radius);
    } else {
        orbit_radius_ = std::clamp(orbit_radius_, min_radius, max_radius);
    }
    const int cx = center->pos.x, cy = center->pos.y;
    const int vx = self_->pos.x - cx, vy = self_->pos.y - cy;
    if (!orbit_params_set_) {
        orbit_angle_ = angle_from_or_random(vx, vy, rng_);
        orbit_params_set_ = true;
    }
    // Set the angular step so that the arc length stays within [min,max] current target step range.
    int step_len_px = rand_int(rng_, std::max(1, min_current_target_dist), std::max(min_current_target_dist, max_current_target_dist));
    double dtheta = static_cast<double>(step_len_px) / std::max(1, orbit_radius_);
    if (dtheta < 0.08) dtheta = 0.08; // ensure some motion
    const double next_angle = orbit_angle_ + static_cast<double>(orbit_dir_) * dtheta;
    int nx = cx + static_cast<int>(std::llround(std::cos(next_angle) * orbit_radius_));
    int ny = cy + static_cast<int>(std::llround(std::sin(next_angle) * orbit_radius_));
    set_target(SDL_Point{nx, ny}, nullptr);
    orbit_angle_ = next_angle;
}

void AnimationUpdate::ensure_patrol_target(const std::vector<SDL_Point>& waypoints,
                                           bool loop,
                                           int hold_frames) 
                                           {
    if (!self_ || waypoints.empty()) return;
    if (!patrol_initialized_ || patrol_points_.size() != waypoints.size()) {
        patrol_points_      = waypoints;
        patrol_index_       = 0;
        patrol_loop_        = loop;
        patrol_hold_frames_ = std::max(0, hold_frames);
        patrol_hold_left_   = patrol_hold_frames_;
        patrol_initialized_ = true;
    }
    if (have_target_ && is_target_reached()) {
        if (patrol_hold_left_ > 0) {
            --patrol_hold_left_;
            return;
        }
        if (patrol_loop_) {
            patrol_index_ = (patrol_index_ + 1) % patrol_points_.size();
        } else if (patrol_index_ + 1 < static_cast<int>(patrol_points_.size())) {
            ++patrol_index_;
        }
        patrol_hold_left_ = patrol_hold_frames_;
    }
    const SDL_Point wp = patrol_points_[patrol_index_];
    // Move toward waypoint in steps limited by [min,max] target step range
    const int sx = self_->pos.x, sy = self_->pos.y;
    const int vx = wp.x - sx,    vy = wp.y - sy;
    const double dist = std::sqrt(static_cast<double>(vx)*vx + static_cast<double>(vy)*vy);
    int take = static_cast<int>(std::round(std::clamp(dist, static_cast<double>(min_current_target_dist), static_cast<double>(max_current_target_dist))));
    if (dist > 1e-6) {
        const double s = static_cast<double>(take) / dist;
        int nx = sx + static_cast<int>(std::llround(static_cast<double>(vx) * s));
        int ny = sy + static_cast<int>(std::llround(static_cast<double>(vy) * s));
        set_target(SDL_Point{nx, ny}, nullptr);
    } else {
        set_target(wp, nullptr);
    }
}

void AnimationUpdate::ensure_serpentine_target(int sway,
                                               const Asset* final_target,
                                               int keep_side_ratio) 
                                            {
    if (!self_ || !final_target) return;
    normalize_minmax(min_current_target_dist, max_current_target_dist);
    sway = std::max(0, sway);
    const int cx = self_->pos.x, cy = self_->pos.y;
    const int tx = final_target->pos.x, ty = final_target->pos.y;
    const int vx = tx - cx,            vy = ty - cy;
    double a = angle_from_or_random(vx, vy, rng_);
    if (serp_params_set_) {
        const int denom = std::max(0, keep_side_ratio) + 1;
        if (rand_int(rng_, 0, denom - 1) == 0) serp_side_ = -serp_side_;
    } else {
        serp_side_ = (rand_int(rng_, 0, 1) ? +1 : -1);
    }
    serp_stride_ = rand_int(rng_, std::max(1, min_current_target_dist), std::max(min_current_target_dist, max_current_target_dist));
    const double bx = static_cast<double>(cx) + static_cast<double>(serp_stride_) * std::cos(a);
    const double by = static_cast<double>(cy) + static_cast<double>(serp_stride_) * std::sin(a);
    double pvx, pvy;
    if (vx == 0 && vy == 0) {
        pvx = -std::sin(a);
        pvy =  std::cos(a);
    } else {
        const double norm = std::sqrt(static_cast<double>(vx)*vx + static_cast<double>(vy)*vy);
        pvx = -static_cast<double>(vy) / norm;
        pvy =  static_cast<double>(vx) / norm;
    }
    double ox = bx + static_cast<double>(serp_side_) * static_cast<double>(sway) * pvx;
    double oy = by + static_cast<double>(serp_side_) * static_cast<double>(sway) * pvy;
    int nx = static_cast<int>(std::llround(ox));
    int ny = static_cast<int>(std::llround(oy));
    set_target(SDL_Point{nx, ny}, final_target);
    serp_params_set_ = true;
}

void AnimationUpdate::ensure_to_point_target() {
    if (!self_) return;
    const double step = std::sqrt(static_cast<double>(min_move_len2()));
    const double d = Range::get_distance(self_->pos, to_point_goal_);
    if (d <= step) {
        auto cb = to_point_on_reach_;
        to_point_on_reach_ = nullptr;
        if (cb) {
            cb(*this);
        } else {
            set_mode_none();
        }
        return;
    }
    set_target(to_point_goal_, nullptr);
}

bool AnimationUpdate::advance(AnimationFrame*& frame) {
    try {
        blocked_last_step_ = false;
        if (!self_ || !self_->info || !frame || self_->static_frame) return true;
        auto it = self_->info->animations.find(self_->current_animation);
        if (it == self_->info->animations.end()) return true;
        Animation& anim = it->second;
        if (anim.frames_data.empty() || anim.number_of_frames <= 0) {
            return false;
        }
        if (anim.index_of(frame) < 0) {
            frame = anim.get_first_frame();
            self_->frame_progress = 0.0f;
            if (!frame) return true;
        }

        float speed = anim.speed_factor;
        if (!std::isfinite(speed)) speed = 1.0f;
        double abs_speed = std::abs(static_cast<double>(speed));
        if (abs_speed < 1e-6) {
            abs_speed = 1.0;
            speed = 1.0f;
        }
        int interval = 1;
        float progress_increment = 1.0f;
        if (abs_speed > 1.0) {
            interval = std::max(1, static_cast<int>(std::lround(abs_speed)));
            progress_increment = 1.0f;
        } else {
            progress_increment = static_cast<float>(abs_speed);
            if (progress_increment <= 0.0f) {
                progress_increment = 1.0f;
            }
        }
        if (slow_frame_interval_ != interval) {
            slow_frame_interval_ = interval;
            slow_frame_counter_ = 0;
        }
        if (slow_frame_interval_ > 1) {
            if (slow_frame_counter_ > 0) {
                --slow_frame_counter_;
                override_movement = false;
                suppress_movement_ = false;
                return true;
            }
            slow_frame_counter_ = slow_frame_interval_ - 1;
        } else {
            slow_frame_counter_ = 0;
        }

        const bool use_override = override_movement;
        const int move_dx = use_override ? dx_ : frame->dx;
        const int move_dy = use_override ? dy_ : frame->dy;
        const bool attempted_move = ((move_dx | move_dy) != 0);
        bool blocked = false;
        if (attempted_move && !suppress_movement_) {
            if (!can_move_by(move_dx, move_dy)) {
                blocked = true;
                blocked_last_step_ = true;
            }
        }
        if (attempted_move && !blocked && !suppress_movement_) {
            self_->pos.x += move_dx;
            self_->pos.y += move_dy;
            if (frame->z_resort) {
                self_->set_z_index();
                Assets* as = assets_owner_;
                if (!as && self_) {
                    as = self_->get_assets();
                }
                if (as) {
                    as->mark_active_assets_dirty();
                }
            }
        }
        override_movement = false;
        suppress_movement_ = false;
        bool reached_end = false;
        self_->frame_progress += progress_increment;
        while (self_->frame_progress >= 1.0f) {
            self_->frame_progress -= 1.0f;
            if (frame->next) {
                frame = frame->next;
            } else if (anim.loop) {
                frame = anim.get_first_frame();
            } else {
                reached_end = true;
                break;
            }
        }
        self_->current_frame = frame;
        return !reached_end;
    } catch (const std::exception& e) {
        std::cerr << "[AnimationUpdate::advance] " << e.what() << "\n";
    } catch (...) {
        std::cerr << "[AnimationUpdate::advance] unknown exception\n";
    }
    return false;
}

void AnimationUpdate::switch_to(const std::string& id) {
    try {
        if (!self_ || !self_->info) return;
        auto it = self_->info->animations.find(id);
        if (it == self_->info->animations.end()) {
            auto def = self_->info->animations.find("default");
            if (def == self_->info->animations.end()) def = self_->info->animations.begin();
            if (def == self_->info->animations.end()) return;
            it = def;
        }
        Animation& anim = it->second;
        if (anim.is_frozen()) return;
        AnimationFrame* new_frame = anim.get_first_frame();
        if (!new_frame) {
            self_->current_animation = it->first;
            self_->current_frame = nullptr;
            self_->static_frame = true;
            self_->frame_progress = 0.0f;
            slow_frame_interval_ = 1;
            slow_frame_counter_ = 0;
            return;
        }
        self_->current_animation = it->first;
        self_->current_frame = new_frame;
        self_->static_frame = anim.is_static();
        self_->frame_progress = 0.0f;
        slow_frame_interval_ = 1;
        slow_frame_counter_ = 0;
        if (anim.has_audio()) {
            AudioEngine::instance().play_now(anim, *self_);
        }
    } catch (const std::exception& e) {
        std::cerr << "[AnimationUpdate::switch_to] " << e.what() << "\n";
    } catch (...) {
        std::cerr << "[AnimationUpdate::switch_to] unknown exception\n";
    }
}

void AnimationUpdate::get_animation() {
    try {
        if (!self_ || !self_->info) return;
        auto it = self_->info->animations.find(self_->current_animation);
        if (it == self_->info->animations.end()) return;
        std::string next = it->second.on_end_mapping;
        if (next.empty()) next = "default";
        if (next == "end") { self_->Delete(); return; }
        if (next == "freeze_on_last") { self_->static_frame = true; return; }
        auto nit = self_->info->animations.find(next);
        if (nit != self_->info->animations.end()) {
            Animation& anim = nit->second;
            AnimationFrame* start = anim.get_first_frame();
            if (!start) return;
            self_->current_animation = next;
            self_->current_frame = start;
            self_->static_frame = anim.is_static();
            self_->frame_progress = 0.0f;
            if (anim.has_audio()) {
                AudioEngine::instance().play_now(anim, *self_);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[AnimationUpdate::get_animation] " << e.what() << "\n";
    } catch (...) {
        std::cerr << "[AnimationUpdate::get_animation] unknown exception\n";
    }
}

void AnimationUpdate::set_animation_now(const std::string& anim_id) {
    if (!self_ || !self_->info) return;
    if (anim_id.empty()) return;
    if (self_->current_animation == anim_id) return;
    queued_anim_.reset();
    if (!mode_suspended_) { saved_mode_ = mode_; mode_suspended_ = true; }
    mode_ = Mode::None;
    have_target_ = false;
    switch_to(anim_id);
    forced_active_ = !self_->static_frame;
}

void AnimationUpdate::move(int x, int y) {
    dx_ = x;
    dy_ = y;
    override_movement = true;
    ManualState& manual = manual_state(this);
    manual.active = true;
    manual.manual_dx = x;
    manual.manual_dy = y;
    if ((x | y) != 0) {
        manual.last_dir_x = x;
        manual.last_dir_y = y;
    }
}

void AnimationUpdate::set_animation_qued(const std::string& anim_id) {
    if (queued_anim_ && *queued_anim_ == anim_id) return;
    queued_anim_ = anim_id;
}

void AnimationUpdate::update() {
    if (!self_ || !self_->info) return;
    try {
        if (forced_active_) {
            bool cont = advance(self_->current_frame);
            if (!cont) {
                forced_active_ = false;
                if (queued_anim_) {
                    switch_to(*queued_anim_);
                    queued_anim_.reset();
                    forced_active_ = !self_->static_frame;
                    if (forced_active_) {
                        advance(self_->current_frame);
                        return;
                    }
                }
                if (mode_suspended_) {
                    mode_ = saved_mode_;
                    mode_suspended_ = false;
                    if (mode_ == Mode::None) {
                        get_animation();
                    }
                } else {
                    if (mode_ == Mode::None) {
                        get_animation();
                    }
                }
            }
            return;
        }
        if (queued_anim_ && self_->is_current_animation_last_frame()) {
            switch_to(*queued_anim_);
            queued_anim_.reset();
            forced_active_ = !self_->static_frame;
            bool cont = advance(self_->current_frame);
            if (!cont) {
                forced_active_ = false;
                if (mode_ == Mode::None) get_animation();
            }
            return;
        }
        if (mode_ != Mode::None) {
            if (!self_->is_current_animation_locked_in_progress()) {
                Mode mode_before = mode_;
                if (!have_target_ || is_target_reached()) {
                    get_new_target();
                    if (mode_ != mode_before) {
                        return;
                    }
                }
                if (!have_target_) {
                    moving = false;
                    const std::string cur = self_->get_current_animation();
                    if (cur != "default") {
                        switch_to("default");
                    }
                    bool cont_idle = advance(self_->current_frame);
                    if (!cont_idle) {
                        get_animation();
                    }
                    return;
                }
                std::string next_anim = (!moving) ? std::string("default") : pick_best_animation_towards(target_);
                moving = true;
                const std::string cur = self_->get_current_animation();
                if (!next_anim.empty() && next_anim != cur) {
                    switch_to(next_anim);
                }
            }
            bool cont = advance(self_->current_frame);
            if (blocked_last_step_) {
                blocked_last_step_ = false;
                moving = false;
                have_target_ = false;
                get_new_target();
                return;
            }
            if (!cont) {
                get_animation();
            }
            return;
        }
        ManualState& manual = manual_state(this);
        if (manual.active) {
            const int mdx = manual.manual_dx;
            const int mdy = manual.manual_dy;
            const bool moving_now = (mdx != 0 || mdy != 0);
            auto has_anim = [&](const char* id) {
                if (!self_->info) return false;
                return self_->info->animations.find(id) != self_->info->animations.end();
};
            auto choose_anim = [&](int dx, int dy) -> std::string {
                if (dx == 0 && dy == 0) return {};
                const int abs_x = std::abs(dx);
                const int abs_y = std::abs(dy);
                auto pick_horizontal = [&]() -> std::string {
                    if (dx > 0) return has_anim("right") ? std::string("right") : std::string{};
                    if (dx < 0) return has_anim("left") ? std::string("left") : std::string{};
                    return std::string{};
};
                auto pick_vertical = [&]() -> std::string {
                    if (dy > 0) return has_anim("forward") ? std::string("forward") : std::string{};
                    if (dy < 0) return has_anim("backward") ? std::string("backward") : std::string{};
                    return std::string{};
};
                if (abs_x > abs_y) {
                    std::string horiz = pick_horizontal();
                    if (!horiz.empty()) return horiz;
                } else if (abs_y > abs_x) {
                    std::string vert = pick_vertical();
                    if (!vert.empty()) return vert;
                } else {
                    const int last_abs_x = std::abs(manual.last_dir_x);
                    const int last_abs_y = std::abs(manual.last_dir_y);
                    if (last_abs_x > last_abs_y) {
                        std::string horiz = pick_horizontal();
                        if (!horiz.empty()) return horiz;
                    } else if (last_abs_y > last_abs_x) {
                        std::string vert = pick_vertical();
                        if (!vert.empty()) return vert;
                    } else if (!manual.last_anim.empty() && manual.last_anim != "default" && has_anim(manual.last_anim.c_str())) {
                        return manual.last_anim;
                    }
                }
                std::string vert = pick_vertical();
                if (!vert.empty()) return vert;
                std::string horiz = pick_horizontal();
                if (!horiz.empty()) return horiz;
                return has_anim("default") ? std::string("default") : std::string{};
};

            std::string desired_anim;
            if (moving_now) {
                desired_anim = choose_anim(mdx, mdy);
                if (!desired_anim.empty()) {
                    if (desired_anim != "default" || manual.last_anim.empty()) {
                        manual.last_anim = desired_anim;
                    }
                }
            } else {
                desired_anim = manual.last_anim;
                if (!desired_anim.empty() && desired_anim != "default" && !has_anim(desired_anim.c_str())) {
                    desired_anim.clear();
                }
                if (desired_anim.empty() || desired_anim == "default") {
                    if (manual.last_anim != "default") {
                        std::string fallback = choose_anim(manual.last_dir_x, manual.last_dir_y);
                        if (!fallback.empty()) {
                            desired_anim = fallback;
                            manual.last_anim = fallback;
                        }
                    }
                    if (desired_anim.empty() && has_anim("default")) {
                        desired_anim = "default";
                    }
                }
            }

            if (!desired_anim.empty() && self_->current_animation != desired_anim) {
                switch_to(desired_anim);
            } else if (!moving_now && manual.last_was_moving) {
                auto it = self_->info->animations.find(self_->current_animation);
                if (it != self_->info->animations.end()) {
                    Animation& anim = it->second;
                    AnimationFrame* first = anim.get_first_frame();
                    if (first) {
                        self_->current_frame = first;
                        self_->frame_progress = 0.0f;
                    }
                }
            }

            if (!moving_now) {
                override_movement = false;
                manual.last_was_moving = false;
                return;
            }

            manual.last_was_moving = true;
        }

        suppress_movement_ = false;
        bool cont = advance(self_->current_frame);
        blocked_last_step_ = false;
        if (!cont) {
            get_animation();
        }
    } catch (const std::exception& e) {
        std::cerr << "[AnimationUpdate::update] " << e.what() << "\n";
    } catch (...) {
        std::cerr << "[AnimationUpdate::update] unknown exception\n";
    }
}

void AnimationUpdate::get_new_target() {
    switch (mode_) {
        case Mode::Idle:       ensure_idle_target(); break;
        case Mode::Pursue:     ensure_pursue_target(pursue_target_); break;
        case Mode::Run:        ensure_run_target(run_threat_); break;
        case Mode::Orbit:      ensure_orbit_target(orbit_min_radius_, orbit_max_radius_, orbit_center_, orbit_keep_ratio_); break;
        case Mode::Patrol:     ensure_patrol_target(patrol_points_, patrol_loop_, patrol_hold_frames_); break;
        case Mode::Serpentine: ensure_serpentine_target(serp_sway_, serp_target_, serp_keep_ratio_); break;
        case Mode::ToPoint:    ensure_to_point_target(); break;
        default: break;
    }
}
