#include "animation_update.hpp"
#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"
#include "animation.hpp"
#include "core/active_assets_manager.hpp"
#include "core/AssetsManager.hpp"
#include "utils/area.hpp"
#include "utils/range_util.hpp"
#include <SDL.h>
#include <limits>
#include <cmath>
#include <algorithm>
#include <random>
#include <string>

// ---------- small local helpers (no header changes) ----------
namespace {

// normalize [min,max] into non-negative and ensure min <= max
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

// angle of (vx,vy); if zero vector, random angle
inline double angle_from_or_random(int vx, int vy, std::mt19937& rng) {
    if (vx == 0 && vy == 0) return rand_angle(rng);
    return std::atan2(static_cast<double>(vy), static_cast<double>(vx));
}

} // namespace

// -------------------------------
// Construction / mode management
// -------------------------------
AnimationUpdate::AnimationUpdate(Asset* self, ActiveAssetsManager& aam, bool confined)
: self_(self), aam_(aam), confined_(confined)
{
    std::seed_seq seed{
        static_cast<unsigned>(reinterpret_cast<uintptr_t>(self) & 0xffffffffu),
        static_cast<unsigned>((reinterpret_cast<uintptr_t>(self) >> 32) & 0xffffffffu)
    };
    rng_.seed(seed);
    weight_dir_    = 0.6;
    weight_sparse_ = 0.4;
}

AnimationUpdate::AnimationUpdate(Asset* self, ActiveAssetsManager& aam, bool confined,
                                 double directness_weight, double sparsity_weight)
: self_(self), aam_(aam), confined_(confined),
  weight_dir_(std::max(0.0, directness_weight)),
  weight_sparse_(std::max(0.0, sparsity_weight))
{
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
}

// -------------------------------
// Map / geometry helpers
// -------------------------------
void AnimationUpdate::clamp_to_room(int& x, int& y) const {
    if (!confined_ || !self_) return;
    Assets* assets = self_->get_assets();
    if (!assets || !assets->current_room_ || !assets->current_room_->room_area) return;
    auto [minx, miny, maxx, maxy] = assets->current_room_->room_area->get_bounds();
    x = std::clamp(x, minx, maxx);
    y = std::clamp(y, miny, maxy);
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

// -------------------------------
// Target selection
// -------------------------------
SDL_Point AnimationUpdate::choose_balanced_target(SDL_Point desired, const Asset* final_target) const {
    if (!self_) return desired;

    const int sx = self_->pos.x;
    const int sy = self_->pos.y;

    SDL_Point aim = desired;
    if (final_target) { aim.x = final_target->pos.x; aim.y = final_target->pos.y; }

    // direction unit toward "aim"
    double fvx = static_cast<double>(aim.x - sx);
    double fvy = static_cast<double>(aim.y - sy);
    double flen = std::sqrt(fvx*fvx + fvy*fvy);
    if (flen > 1e-6) { fvx /= flen; fvy /= flen; } else { fvx = 1.0; fvy = 0.0; }

    // base polar from current -> desired
    const int dx0 = desired.x - sx;
    const int dy0 = desired.y - sy;
    const double base_angle  = std::atan2(static_cast<double>(dy0), static_cast<double>(dx0));
    double base_radius = std::sqrt(static_cast<double>(dx0*dx0 + dy0*dy0));
    if (base_radius < 1.0) base_radius = 1.0;

    // Neighborhood via Range
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
            clamp_to_room(px, py);

            const double dir_norm = Range::get_distance(SDL_Point{px, py}, aim) / rline;

            double sum = 0.0; int cnt = 0;
            for (Asset* n : neighbors) {
                if (!n || n == self_ || !n->info) continue;
                if (final_target && n == final_target) continue;
                if (n->info->has_tag("ground")) continue;

                const double rvx = static_cast<double>(n->pos.x - sx);
                const double rvy = static_cast<double>(n->pos.y - sy);
                if (rvx*fvx + rvy*fvy <= 0.0) continue; // behind us w.r.t. aim

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

void AnimationUpdate::set_target(SDL_Point desired, const Asset* final_target) {
    target_ = choose_balanced_target(desired, final_target);
    have_target_ = true;
}

// -------------------------------
// Public knobs (now no-op if already in that Mode)
// -------------------------------
void AnimationUpdate::set_weights(double directness_weight, double sparsity_weight) {
    weight_dir_    = std::max(0.0, directness_weight);
    weight_sparse_ = std::max(0.0, sparsity_weight);
}

void AnimationUpdate::set_idle(int min_target_distance, int max_target_distance, int rest_ratio) {
    if (mode_ == Mode::Idle) return;
    idle_min_dist_   = min_target_distance;
    idle_max_dist_   = max_target_distance;
    idle_rest_ratio_ = rest_ratio;
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

// -------------------------------
/* Collision / overlap guards */
// -------------------------------
bool AnimationUpdate::can_move_by(int dx, int dy) const {
    if (!self_ || !self_->info) return false;

    const int test_x = self_->pos.x + dx;
    const int test_y = self_->pos.y + dy - self_->info->z_threshold;

    for (Asset* a : aam_.getImpassableClosest()) {
        if (!a || a == self_) continue;
        Area obstacle = a->get_area("passability");
        if (obstacle.contains_point({ test_x, test_y })) {
            return false;
        }
    }
    return true;
}

bool AnimationUpdate::would_overlap_same_or_player(int dx, int dy) const {
    if (!self_ || !self_->info) return true;

    const SDL_Point new_pos{ self_->pos.x + dx, self_->pos.y + dy };
    const auto& active = aam_.getActive();

    for (Asset* a : active) {
        if (!a || a == self_ || !a->info) continue;

        const bool is_enemy  = (a->info->type == "enemy");
        const bool is_player = (a->info->type == "Player");
        if (!is_enemy && !is_player) continue;

        if (Range::get_distance(new_pos, a) < 40.0) {
            return true;
        }
    }
    return false;
}

// -------------------------------
// Animation chooser
// -------------------------------
std::string AnimationUpdate::pick_best_animation_towards(SDL_Point target) const {
    if (!self_ || !self_->info) return {};
    const auto& all = self_->info->animations;
    if (all.empty()) return {};

    double best_d = std::numeric_limits<double>::infinity();
    std::string best_id;

    for (const auto& kv : all) {
        const std::string& id  = kv.first;
        const Animation& anim  = kv.second;
        const int dx = anim.total_dx;
        const int dy = anim.total_dy;
        if (dx == 0 && dy == 0) continue;
        if (!can_move_by(dx, dy)) continue;
        if (would_overlap_same_or_player(dx, dy)) continue;

        const SDL_Point next{ self_->pos.x + dx, self_->pos.y + dy };
        const double d = Range::get_distance(next, target);
        if (d < best_d) {
            best_d = d;
            best_id = id;
        }
    }
    return best_id;
}

// -------------------------------
// Target ensure implementations (use helpers to reduce repetition)
// -------------------------------
void AnimationUpdate::ensure_idle_target(int min_dist, int max_dist) {
    int cx = self_ ? self_->pos.x : 0;
    int cy = self_ ? self_->pos.y : 0;
    normalize_minmax(min_dist, max_dist);

    const double a = rand_angle(rng_);
    const double r = rand_real(rng_, static_cast<double>(min_dist), static_cast<double>(max_dist));

    int tx = cx + static_cast<int>(std::llround(r * std::cos(a)));
    int ty = cy + static_cast<int>(std::llround(r * std::sin(a)));
    clamp_to_room(tx, ty);
    set_target(SDL_Point{tx, ty}, nullptr);
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
    clamp_to_room(nx, ny);
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
    clamp_to_room(nx, ny);
    set_target(SDL_Point{nx, ny}, nullptr);
}

void AnimationUpdate::ensure_orbit_target(int min_radius, int max_radius, const Asset* center, int keep_direction_ratio) {
    if (!self_ || !center) return;
    normalize_minmax(min_radius, max_radius);

    // direction pick/flip
    if (orbit_params_set_) {
        const int denom = std::max(0, keep_direction_ratio) + 1;
        if (rand_int(rng_, 0, denom - 1) == 0) orbit_dir_ = -orbit_dir_;
    } else {
        orbit_dir_ = (rand_int(rng_, 0, 1) ? +1 : -1);
    }

    // radius
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
    clamp_to_room(nx, ny);

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
    int nx = wp.x, ny = wp.y;
    clamp_to_room(nx, ny);
    set_target(SDL_Point{nx, ny}, nullptr);
}

void AnimationUpdate::ensure_serpentine_target(int min_stride,
                                               int max_stride,
                                               int sway,
                                               const Asset* final_target,
                                               int keep_side_ratio)
{
    if (!self_ || !final_target) return;
    normalize_minmax(min_stride, max_stride);
    sway = std::max(0, sway);

    const int cx = self_->pos.x, cy = self_->pos.y;
    const int tx = final_target->pos.x, ty = final_target->pos.y;
    const int vx = tx - cx,            vy = ty - cy;

    double a = angle_from_or_random(vx, vy, rng_);

    // side persistence / flip
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
    clamp_to_room(nx, ny);

    set_target(SDL_Point{nx, ny}, final_target);
    serp_params_set_ = true;
}

bool AnimationUpdate::advance(AnimationFrame*& frame) {
    if (!self_ || !self_->info || !frame) return true;

    auto it = self_->info->animations.find(self_->current_animation);
    if (it == self_->info->animations.end()) return true;
    Animation& anim = it->second;

    self_->pos.x += frame->dx;
    self_->pos.y += frame->dy;

    if ((frame->dx || frame->dy) && frame->z_resort) {
        self_->set_z_index();
        if (Assets* as = self_->get_assets()) {
            as->activeManager.sortByZIndex();
        }
    }

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
}

void AnimationUpdate::switch_to(const std::string& id) {
    if (!self_ || !self_->info) return;
    auto it = self_->info->animations.find(id);
    if (it == self_->info->animations.end()) return;
    self_->current_animation = id;
    Animation& anim = it->second;
    anim.change(self_->current_frame, self_->static_frame);
    self_->frame_progress = 0.0f;
}

void AnimationUpdate::get_animation() {
    if (!self_ || !self_->info) return;
    auto it = self_->info->animations.find(self_->current_animation);
    if (it == self_->info->animations.end()) return;
    std::string next = it->second.on_end_mapping;
    if (next.empty()) next = "default";
    if (next == "end") { self_->Delete(); return; }
    if (next == "freeze_on_last") { self_->static_frame = true; return; }
    auto nit = self_->info->animations.find(next);
    if (nit != self_->info->animations.end()) {
        self_->current_animation = next;
        Animation& anim = nit->second;
        anim.change(self_->current_frame, self_->static_frame);
        self_->frame_progress = 0.0f;
    }
}

void AnimationUpdate::set_animation_now(const std::string& anim_id) {
    if (!self_ || !self_->info) return;
    if (anim_id.empty()) return;
    if (self_->current_animation == anim_id) return;
    queued_anim_.reset();
    forced_active_ = true;
    if (!mode_suspended_) { saved_mode_ = mode_; mode_suspended_ = true; }
    mode_ = Mode::None;
    have_target_ = false;
    switch_to(anim_id);
}

void AnimationUpdate::set_animation_qued(const std::string& anim_id) {
    if (queued_anim_ && *queued_anim_ == anim_id) return;
    queued_anim_ = anim_id;
}

// -------------------------------
// Full update orchestration
// -------------------------------
void AnimationUpdate::update() {
    if (!self_ || !self_->info) return;

    // If currently playing a forced/queued animation
    if (forced_active_) {
        bool cont = advance(self_->current_frame);
        if (!cont) {
            forced_active_ = false;
            if (queued_anim_) {
                switch_to(*queued_anim_);
                queued_anim_.reset();
                forced_active_ = true;
                advance(self_->current_frame);
                return;
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

    // If a queued animation is waiting and the current finished
    if (queued_anim_ && self_->is_current_animation_last_frame()) {
        switch_to(*queued_anim_);
        queued_anim_.reset();
        forced_active_ = true;
        bool cont = advance(self_->current_frame);
        if (!cont) {
            forced_active_ = false;
            if (mode_ == Mode::None) get_animation();
        }
        return;
    }

    // Normal mode-driven update
    if (mode_ != Mode::None) {
        if (!self_->is_current_animation_locked_in_progress()) {
            if (!have_target_ || is_target_reached()) {
                switch (mode_) {
                    case Mode::Idle:       ensure_idle_target(idle_min_dist_, idle_max_dist_); break;
                    case Mode::Pursue:     ensure_pursue_target(pursue_min_dist_, pursue_max_dist_, pursue_target_); break;
                    case Mode::Run:        ensure_run_target(run_min_dist_, run_max_dist_, run_threat_); break;
                    case Mode::Orbit:      ensure_orbit_target(orbit_min_radius_, orbit_max_radius_, orbit_center_, orbit_keep_ratio_); break;
                    case Mode::Patrol:     ensure_patrol_target(patrol_points_, patrol_loop_, patrol_hold_frames_); break;
                    case Mode::Serpentine: ensure_serpentine_target(serp_min_stride_, serp_max_stride_, serp_sway_, serp_target_, serp_keep_ratio_); break;
                    default: break;
                }
            }

            const std::string next_anim = pick_best_animation_towards(target_);
            const std::string cur = self_->get_current_animation();
            if (!next_anim.empty() && next_anim != cur) {
                switch_to(next_anim);
            }
        }
        advance(self_->current_frame);
        return;
    }

    // No mode: just advance current animation
    bool cont = advance(self_->current_frame);
    if (!cont) {
        get_animation();
    }
}
