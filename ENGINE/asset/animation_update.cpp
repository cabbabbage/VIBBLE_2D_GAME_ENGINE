#include "animation_update.hpp"
#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"
#include "asset/asset_types.hpp"
#include "animation.hpp"
#include "core/active_assets_manager.hpp"
#include "core/AssetsManager.hpp"
#include "audio/audio_engine.hpp"
#include "utils/area.hpp"
#include "utils/range_util.hpp"
#include <SDL.h>
#include <limits>
#include <cmath>
#include <algorithm>
#include <random>
#include <string>
#include <iostream>

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

using ManualState = AnimationUpdate::ManualState;
}

AnimationUpdate::AnimationUpdate(Asset* self, ActiveAssetsManager& aam)
: self_(self), aam_(aam) {
    std::seed_seq seed{
        static_cast<unsigned>(reinterpret_cast<uintptr_t>(self) & 0xffffffffu),
        static_cast<unsigned>((reinterpret_cast<uintptr_t>(self) >> 32) & 0xffffffffu)
    };
    rng_.seed(seed);
    weight_dir_    = 0.6;
    weight_sparse_ = 0.4;
}

AnimationUpdate::AnimationUpdate(Asset* self, ActiveAssetsManager& aam,
                                 double directness_weight, double sparsity_weight)
: self_(self), aam_(aam),
  weight_dir_(std::max(0.0, directness_weight)),
  weight_sparse_(std::max(0.0, sparsity_weight)) {
    std::seed_seq seed{
        static_cast<unsigned>(reinterpret_cast<uintptr_t>(self) & 0xffffffffu),
        static_cast<unsigned>((reinterpret_cast<uintptr_t>(self) >> 32) & 0xffffffffu)
    };
    rng_.seed(seed);
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

SDL_Point AnimationUpdate::choose_balanced_target(SDL_Point desired, const Asset* final_target) const {
    if (!self_) return desired;
    const int sx = self_->pos.x;
    const int sy = self_->pos.y;
    SDL_Point aim = desired;
    if (final_target) { aim.x = final_target->pos.x; aim.y = final_target->pos.y; }
    double fvx = static_cast<double>(aim.x - sx);
    double fvy = static_cast<double>(aim.y - sy);
    double flen = std::sqrt(fvx*fvx + fvy*fvy);
    if (flen > 1e-6) { fvx /= flen; fvy /= flen; } else { fvx = 1.0; fvy = 0.0; }
    const int dx0 = desired.x - sx;
    const int dy0 = desired.y - sy;
    const double base_angle  = std::atan2(static_cast<double>(dy0), static_cast<double>(dx0));
    double base_radius = std::sqrt(static_cast<double>(dx0*dx0 + dy0*dy0));
    if (base_radius < 1.0) base_radius = 1.0;
    const auto& active = aam_.getActive();
    std::vector<Asset*> neighbors;
    neighbors.reserve(active.size());
    Range::get_in_range(SDL_Point{sx, sy}, 300, active, neighbors);
    static const double ang_offsets[] = { -0.6, -0.4, -0.25, -0.12, 0.0, 0.12, 0.25, 0.4, 0.6 };
    static const double rad_scales[]  = { 0.9, 1.0, 1.1 };
    SDL_Point best = desired;
    double best_cost = std::numeric_limits<double>::infinity();
    const double rline = std::max(1.0, Range::get_distance(SDL_Point{sx, sy}, aim));
    const double sparse_cap = 300.0;
    const double w_dir   = std::max(0.0, weight_dir_);
    const double w_sparse= std::max(0.0, weight_sparse_);
    for (double dth : ang_offsets) {
        for (double rs : rad_scales) {
            const double ang = base_angle + dth;
            const double rr  = base_radius * rs;
            int px = sx + static_cast<int>(std::llround(rr * std::cos(ang)));
            int py = sy + static_cast<int>(std::llround(rr * std::sin(ang)));
            const double dir_norm = Range::get_distance(SDL_Point{px, py}, aim) / rline;
            double sum = 0.0; int cnt = 0;
            for (Asset* n : neighbors) {
                if (!n || n == self_ || !n->info) continue;
                if (final_target && n == final_target) continue;
                if (n->info->type == asset_types::texture) continue;
                if (n->info->passable) continue;
                const double rvx = static_cast<double>(n->pos.x - sx);
                const double rvy = static_cast<double>(n->pos.y - sy);
                if (rvx*fvx + rvy*fvy <= 0.0) continue;
                const double d = Range::get_distance(SDL_Point{px, py}, n);
                sum += std::min(d, sparse_cap);
                ++cnt;
            }
            const double avg_sparse = (cnt > 0) ? (sum / cnt) : sparse_cap;
            const double sparse_norm = avg_sparse / sparse_cap;
            const double cost = w_dir * dir_norm - w_sparse * sparse_norm;
            if (cost < best_cost) {
                best_cost = cost;
                best = SDL_Point{px, py};
            }
        }
    }
    return best;
}

SDL_Point AnimationUpdate::bottom_middle(SDL_Point pos) const {
    if (!self_ || !self_->info) return pos;
    return SDL_Point{ pos.x, pos.y - self_->info->z_threshold };
}

bool AnimationUpdate::point_in_impassable(SDL_Point pt, const Asset* ignored) const {
    // Only test the closest relevant asset; use a robust lookup of the
    // impassable/collision area and rely on Asset::get_area to align + scale.
    Asset* closest = nullptr;
    double best_d2 = std::numeric_limits<double>::infinity();

    const auto& active = aam_.getActive();
    for (Asset* a : active) {
        if (!a || a == self_ || a == ignored || !a->info) continue;
        if (a->info->type == asset_types::texture) continue;
        if (a->info->passable) continue; // Only consider impassable-tagged assets
        const double dx = static_cast<double>(a->pos.x - pt.x);
        const double dy = static_cast<double>(a->pos.y - pt.y);
        const double d2 = dx*dx + dy*dy;
        if (d2 < best_d2) { best_d2 = d2; closest = a; }
    }

    if (!closest) return false;

    // Try known area names in order of likelihood
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

SDL_Point AnimationUpdate::sanitize_target(SDL_Point desired, const Asset* final_target) const {
    if (!self_ || !self_->info) return desired;
    SDL_Point origin = bottom_middle(self_->pos);
    auto is_valid = [&](SDL_Point candidate) {
        SDL_Point bottom = bottom_middle(candidate);
        if (point_in_impassable(bottom, final_target)) return false;
        if (path_blocked(origin, bottom, final_target)) return false;
        return true;
    };
    if (is_valid(desired)) return desired;
    const int base_step = std::max(1, static_cast<int>(std::lround(std::sqrt(static_cast<double>(min_move_len2())))));
    const int max_attempts = 12;
    for (int attempt = 1; attempt <= max_attempts; ++attempt) {
        const int offset = base_step * attempt;
        for (int dir : {-1, 1}) {
            SDL_Point candidate{ desired.x + dir * offset, desired.y };
            if (is_valid(candidate)) {
                return candidate;
            }
        }
    }
    return desired;
}

void AnimationUpdate::set_target(SDL_Point desired, const Asset* final_target) {
    SDL_Point chosen = choose_balanced_target(desired, final_target);
    target_ = sanitize_target(chosen, final_target);
    have_target_ = true;
}

void AnimationUpdate::set_weights(double directness_weight, double sparsity_weight) {
    weight_dir_    = std::max(0.0, directness_weight);
    weight_sparse_ = std::max(0.0, sparsity_weight);
}

void AnimationUpdate::set_idle(int min_target_distance, int max_target_distance, int rest_ratio) {
    if (mode_ == Mode::Idle) return;
    if (max_target_distance < min_target_distance) std::swap(min_target_distance, max_target_distance);
    idle_min_dist_   = std::max(0, min_target_distance);
    idle_max_dist_   = std::max(0, max_target_distance);
    idle_rest_ratio_ = std::clamp(rest_ratio, 0, 100);
    transition_mode(Mode::Idle);
}

void AnimationUpdate::set_pursue(Asset* final_target, int min_target_distance, int max_target_distance) {
    if (mode_ == Mode::Pursue) return;
    pursue_target_   = final_target;
    pursue_min_dist_ = min_target_distance;
    pursue_max_dist_ = max_target_distance;
    transition_mode(Mode::Pursue);
}

void AnimationUpdate::set_run(Asset* threat, int min_target_distance, int max_target_distance) {
    if (mode_ == Mode::Run) return;
    run_threat_   = threat;
    run_min_dist_ = min_target_distance;
    run_max_dist_ = max_target_distance;
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

void AnimationUpdate::set_serpentine(Asset* final_target, int min_stride, int max_stride, int sway, int keep_side_ratio) {
    if (mode_ == Mode::Serpentine) return;
    serp_target_      = final_target;
    serp_min_stride_  = min_stride;
    serp_max_stride_  = max_stride;
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
    const auto& active = aam_.getActive();
    for (Asset* a : active) {
        if (!a || a == self_ || !a->info) continue;
        const bool is_enemy  = (a->info->type == asset_types::enemy);
        const bool is_player = (a->info->type == asset_types::player);
        if (!is_enemy && !is_player) continue;
        if (Range::get_distance(new_pos, a) < 40.0) {
            return true;
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
        // When moving, consider any animation that results in movement (dx, dy not both zero).
        // When idle, consider only animations that have no movement (dx == 0 && dy == 0).
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

void AnimationUpdate::ensure_idle_target(int min_dist, int max_dist) {
    if (!self_) return;
    if (!self_->is_current_animation_last_frame()) return;
    normalize_minmax(min_dist, max_dist);
    const int rest_pct = std::clamp(idle_rest_ratio_, 0, 100);
    const double roll = rand_real(rng_, 0.0, 100.0);
    if (roll < static_cast<double>(rest_pct)) {
        moving = false;
        return;
    }
    const int cx = self_->pos.x;
    const int cy = self_->pos.y;
    const double a = rand_angle(rng_);
    const double r = rand_real(rng_, static_cast<double>(min_dist), static_cast<double>(max_dist));
    const int tx = cx + static_cast<int>(std::llround(r * std::cos(a)));
    const int ty = cy + static_cast<int>(std::llround(r * std::sin(a)));
    moving = true;
    set_target(SDL_Point{ tx, ty }, nullptr);
}

void AnimationUpdate::ensure_pursue_target(int min_dist, int max_dist, const Asset* final_target) {
    if (!self_ || !final_target) return;
    normalize_minmax(min_dist, max_dist);
    const int cx = self_->pos.x, cy = self_->pos.y;
    const int tx = final_target->pos.x, ty = final_target->pos.y;
    const double a = angle_from_or_random(tx - cx, ty - cy, rng_);
    const double r = rand_real(rng_, static_cast<double>(min_dist), static_cast<double>(max_dist));
    int nx = cx + static_cast<int>(std::llround(r * std::cos(a)));
    int ny = cy + static_cast<int>(std::llround(r * std::sin(a)));
    set_target(SDL_Point{nx, ny}, final_target);
}

void AnimationUpdate::ensure_run_target(int min_dist, int max_dist, const Asset* threat) {
    if (!self_ || !threat) return;
    normalize_minmax(min_dist, max_dist);
    const int cx = self_->pos.x, cy = self_->pos.y;
    const int tx = threat->pos.x, ty = threat->pos.y;
    const double a = angle_from_or_random(cx - tx, cy - ty, rng_);
    const double r = rand_real(rng_, static_cast<double>(min_dist), static_cast<double>(max_dist));
    int nx = cx + static_cast<int>(std::llround(r * std::cos(a)));
    int ny = cy + static_cast<int>(std::llround(r * std::sin(a)));
    set_target(SDL_Point{nx, ny}, nullptr);
}

void AnimationUpdate::ensure_orbit_target(int min_radius, int max_radius, const Asset* center, int keep_direction_ratio) {
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
        orbit_dir_ = orbit_force_dir_ ? ((orbit_forced_dir_ >= 0) ? +1 : -1)
                                      : (rand_int(rng_, 0, 1) ? +1 : -1);
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
    const double step_len = std::max(1.0, std::sqrt(static_cast<double>(min_move_len2())));
    double dtheta = step_len / std::max(1, orbit_radius_);
    if (dtheta < 0.08) dtheta = 0.08;
    const double next_angle = orbit_angle_ + static_cast<double>(orbit_dir_) * dtheta;
    int nx = cx + static_cast<int>(std::llround(std::cos(next_angle) * orbit_radius_));
    int ny = cy + static_cast<int>(std::llround(std::sin(next_angle) * orbit_radius_));
    set_target(SDL_Point{nx, ny}, nullptr);
    orbit_angle_ = next_angle;
}

void AnimationUpdate::set_orbit_ccw(Asset* center, int min_radius, int max_radius) {
    orbit_center_     = center;
    orbit_min_radius_ = min_radius;
    orbit_max_radius_ = max_radius;
    orbit_keep_ratio_ = 1000000;
    orbit_force_dir_  = true;
    orbit_forced_dir_ = +1;
    transition_mode(Mode::Orbit);
}

void AnimationUpdate::set_orbit_cw(Asset* center, int min_radius, int max_radius) {
    orbit_center_     = center;
    orbit_min_radius_ = min_radius;
    orbit_max_radius_ = max_radius;
    orbit_keep_ratio_ = 1000000;
    orbit_force_dir_  = true;
    orbit_forced_dir_ = -1;
    transition_mode(Mode::Orbit);
}

void AnimationUpdate::ensure_patrol_target(const std::vector<SDL_Point>& waypoints,
                                           bool loop,
                                           int hold_frames) {
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
    int nx = wp.x, ny = wp.y;
    set_target(SDL_Point{nx, ny}, nullptr);
}

void AnimationUpdate::ensure_serpentine_target(int min_stride,
                                               int max_stride,
                                               int sway,
                                               const Asset* final_target,
                                               int keep_side_ratio) {
    if (!self_ || !final_target) return;
    normalize_minmax(min_stride, max_stride);
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
    serp_stride_ = rand_int(rng_, min_stride, max_stride);
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
                if (Assets* as = self_->get_assets()) {
                    as->active_manager().markNeedsSort();
                }
            }
        }
        override_movement = false;
        suppress_movement_ = false;
        bool reached_end = false;
        self_->frame_progress += anim.speed_factor;
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
            return;
        }
        self_->current_animation = it->first;
        self_->current_frame = new_frame;
        self_->static_frame = anim.is_static();
        self_->frame_progress = 0.0f;
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
    ManualState& manual = manual_state_;
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
        ManualState& manual = manual_state_;
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
        // Manual control should allow the override movement to actually update the
        // asset position. Previously we set `suppress_movement_` to
        // `manual.active`, which is always true inside this branch and therefore
        // prevented any positional changes from occurring. As a consequence,
        // assets controlled via `AnimationUpdate::move` (e.g. the player and
        // custom controllers that rely on manual movement) never moved despite
        // providing non-zero deltas. Clearing the suppression flag here ensures
        // that the overridden deltas are applied during `advance`.
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
        case Mode::Idle:       ensure_idle_target(idle_min_dist_, idle_max_dist_); break;
        case Mode::Pursue:     ensure_pursue_target(pursue_min_dist_, pursue_max_dist_, pursue_target_); break;
        case Mode::Run:        ensure_run_target(run_min_dist_, run_max_dist_, run_threat_); break;
        case Mode::Orbit:      ensure_orbit_target(orbit_min_radius_, orbit_max_radius_, orbit_center_, orbit_keep_ratio_); break;
        case Mode::Patrol:     ensure_patrol_target(patrol_points_, patrol_loop_, patrol_hold_frames_); break;
        case Mode::Serpentine: ensure_serpentine_target(serp_min_stride_, serp_max_stride_, serp_sway_, serp_target_, serp_keep_ratio_); break;
        case Mode::ToPoint:    ensure_to_point_target(); break;
        default: break;
    }
}
