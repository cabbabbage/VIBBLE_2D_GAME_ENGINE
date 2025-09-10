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
#include <iostream>

AnimationUpdate::AnimationUpdate(Asset* self, ActiveAssetsManager& aam, bool confined)
: self_(self), aam_(aam), confined_(confined)
{
	std::seed_seq seed{ static_cast<unsigned>(reinterpret_cast<uintptr_t>(self) & 0xffffffffu),
		static_cast<unsigned>((reinterpret_cast<uintptr_t>(self) >> 32) & 0xffffffffu) };
	rng_.seed(seed);
	weight_dir_ = 0.6;
	weight_sparse_ = 0.4;
}

AnimationUpdate::AnimationUpdate(Asset* self, ActiveAssetsManager& aam, bool confined,
                           double directness_weight, double sparsity_weight)
: self_(self), aam_(aam), confined_(confined),
weight_dir_(directness_weight), weight_sparse_(sparsity_weight)
{
	std::seed_seq seed{ static_cast<unsigned>(reinterpret_cast<uintptr_t>(self) & 0xffffffffu),
		static_cast<unsigned>((reinterpret_cast<uintptr_t>(self) >> 32) & 0xffffffffu) };
	rng_.seed(seed);
}

void AnimationUpdate::transition_mode(Mode m) {
	if (mode_ == m) return;
	mode_ = m;
	have_target_ = false;
	if (m != Mode::Orbit) orbit_params_set_ = false;
	if (m != Mode::Serpentine) serp_params_set_ = false;
	if (m != Mode::Patrol) patrol_initialized_ = false;
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
	if (flen > 1e-6) { fvx /= flen; fvy /= flen; }
	else { fvx = 1.0; fvy = 0.0; }
	int dx0 = desired.x - sx;
	int dy0 = desired.y - sy;
	double base_angle = std::atan2(static_cast<double>(dy0), static_cast<double>(dx0));
	double base_radius = std::sqrt(static_cast<double>(dx0*dx0 + dy0*dy0));
	if (base_radius < 1.0) base_radius = 1.0;
	const auto& nearby = aam_.getClosest();
	const auto& active = aam_.getActive();
	std::vector<Asset*> neighbors;
	neighbors.reserve(nearby.size());
	for (Asset* a : nearby) neighbors.push_back(a);
	if (neighbors.empty()) for (Asset* a : active) neighbors.push_back(a);
	static const double ang_offsets[] = { -0.6, -0.4, -0.25, -0.12, 0.0, 0.12, 0.25, 0.4, 0.6 };
	static const double rad_scales[]  = { 0.9, 1.0, 1.1 };
	SDL_Point best = desired;
	double best_cost = std::numeric_limits<double>::infinity();
	const double rline = std::max(1.0, Range::get_distance(SDL_Point{sx, sy}, aim));
	const double sparse_cap = 300.0;
	const double w_dir = std::max(0.0, weight_dir_);
	const double w_sparse = std::max(0.0, weight_sparse_);
	for (double dth : ang_offsets) {
		for (double rs : rad_scales) {
			double ang = base_angle + dth;
			double rr = base_radius * rs;
			int cx = sx + static_cast<int>(std::llround(rr * std::cos(ang)));
			int cy = sy + static_cast<int>(std::llround(rr * std::sin(ang)));
			int px = cx, py = cy;
			clamp_to_room(px, py);
			double d_dir = Range::get_distance(SDL_Point{px, py}, aim);
			double dir_norm = d_dir / rline;
			double sum = 0.0; int cnt = 0;
			for (Asset* n : neighbors) {
				if (!n || n == self_) continue;
				if (final_target && n == final_target) continue;
				if (!n->info) continue;
				if (n->info->has_tag("ground")) continue;
				int nx = n->pos.x, ny = n->pos.y;
				double rvx = static_cast<double>(nx - sx);
				double rvy = static_cast<double>(ny - sy);
				double dot = rvx*fvx + rvy*fvy;
				if (dot <= 0.0) continue;
				double d = Range::get_distance(SDL_Point{px, py}, SDL_Point{nx, ny});
				sum += std::min(d, sparse_cap);
				cnt += 1;
			}
			double avg = (cnt > 0) ? (sum / cnt) : sparse_cap;
			double sparse_norm = avg / sparse_cap;
			double cost = w_dir * dir_norm - w_sparse * sparse_norm;
			if (cost < best_cost) { best_cost = cost; best = SDL_Point{ px, py }; }
		}
	}
	return best;
}

void AnimationUpdate::set_target(SDL_Point desired, const Asset* final_target) {
	SDL_Point pick = choose_balanced_target(desired, final_target);
	target_ = pick;
	have_target_ = true;
}

void AnimationUpdate::set_weights(double directness_weight, double sparsity_weight) {
	weight_dir_ = std::max(0.0, directness_weight);
	weight_sparse_ = std::max(0.0, sparsity_weight);
}

void AnimationUpdate::set_idle(int min_target_distance, int max_target_distance, int rest_ratio) {
	idle_min_dist_ = min_target_distance;
	idle_max_dist_ = max_target_distance;
	idle_rest_ratio_ = rest_ratio;
	transition_mode(Mode::Idle);
}

void AnimationUpdate::set_pursue(Asset* final_target, int min_target_distance, int max_target_distance) {
	pursue_target_ = final_target;
	pursue_min_dist_ = min_target_distance;
	pursue_max_dist_ = max_target_distance;
	transition_mode(Mode::Pursue);
}

void AnimationUpdate::set_run(Asset* threat, int min_target_distance, int max_target_distance) {
	run_threat_ = threat;
	run_min_dist_ = min_target_distance;
	run_max_dist_ = max_target_distance;
	transition_mode(Mode::Run);
}

void AnimationUpdate::set_orbit(Asset* center, int min_radius, int max_radius, int keep_direction_ratio) {
	orbit_center_ = center;
	orbit_min_radius_ = min_radius;
	orbit_max_radius_ = max_radius;
	orbit_keep_ratio_ = keep_direction_ratio;
	transition_mode(Mode::Orbit);
}

void AnimationUpdate::set_patrol(const std::vector<SDL_Point>& waypoints, bool loop, int hold_frames) {
	patrol_points_ = waypoints;
	patrol_loop_ = loop;
	patrol_hold_frames_ = std::max(0, hold_frames);
	patrol_initialized_ = false;
	transition_mode(Mode::Patrol);
}

void AnimationUpdate::set_serpentine(Asset* final_target, int min_stride, int max_stride, int sway, int keep_side_ratio) {
	serp_target_ = final_target;
	serp_min_stride_ = min_stride;
	serp_max_stride_ = max_stride;
	serp_sway_ = sway;
	serp_keep_ratio_ = keep_side_ratio;
	transition_mode(Mode::Serpentine);
}

void AnimationUpdate::move() {
        if (!self_) return;
        if (self_->is_current_animation_locked_in_progress()) {
                return;
        }
        switch (mode_) {
                case Mode::Idle: {
                        ensure_idle_target(idle_min_dist_, idle_max_dist_);
                        int denom = std::max(0, idle_rest_ratio_) + 1;
                        std::uniform_int_distribution<int> pick(0, denom - 1);
                        bool choose_rest = (pick(rng_) != 0);
                        std::string next_anim = pick_best_animation_towards(target_);
                        const std::string cur = self_->get_current_animation();
                        if (self_->next_animation.empty()) {
                                if (!choose_rest && !next_anim.empty() && next_anim != cur) {
                                        self_->change_animation_qued(next_anim);
                                } else if (cur != "default") {
                                        self_->change_animation_qued("default");
                                }
                        }
                        break;
                }
                case Mode::Pursue: {
                        ensure_pursue_target(pursue_min_dist_, pursue_max_dist_, pursue_target_);
                        std::string next_anim = pick_best_animation_towards(target_);
                        if (self_->next_animation.empty() && !next_anim.empty() && next_anim != self_->get_current_animation()) {
                                self_->change_animation_qued(next_anim);
                        }
                        break;
                }
                case Mode::Run: {
                        ensure_run_target(run_min_dist_, run_max_dist_, run_threat_);
                        std::string next_anim = pick_best_animation_towards(target_);
                        if (self_->next_animation.empty() && !next_anim.empty() && next_anim != self_->get_current_animation()) {
                                self_->change_animation_qued(next_anim);
                        }
                        break;
                }
                case Mode::Orbit: {
                        ensure_orbit_target(orbit_min_radius_, orbit_max_radius_, orbit_center_, orbit_keep_ratio_);
                        std::string next_anim = pick_best_animation_towards(target_);
                        if (self_->next_animation.empty() && !next_anim.empty() && next_anim != self_->get_current_animation()) {
                                self_->change_animation_qued(next_anim);
                        }
                        break;
                }
                case Mode::Patrol: {
                        ensure_patrol_target(patrol_points_, patrol_loop_, patrol_hold_frames_);
                        std::string next_anim = pick_best_animation_towards(target_);
                        if (self_->next_animation.empty() && !next_anim.empty() && next_anim != self_->get_current_animation()) {
                                self_->change_animation_qued(next_anim);
                        }
                        break;
                }
                case Mode::Serpentine: {
                        ensure_serpentine_target(serp_min_stride_, serp_max_stride_, serp_sway_, serp_target_, serp_keep_ratio_);
                        std::string next_anim = pick_best_animation_towards(target_);
                        if (self_->next_animation.empty() && !next_anim.empty() && next_anim != self_->get_current_animation()) {
                                self_->change_animation_qued(next_anim);
                        }
                        break;
                }
                case Mode::None:
                default:
                break;
        }
}

bool AnimationUpdate::can_move_by(int dx, int dy) const {
	if (!self_ || !self_->info) return false;
	int test_x = self_->pos.x + dx;
	int test_y = self_->pos.y + dy - self_->info->z_threshold;
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

    SDL_Point new_pos{ self_->pos.x + dx, self_->pos.y + dy };
    const auto& active = aam_.getActive();

    for (Asset* a : active) {
        if (!a || a == self_ || !a->info) continue;

        bool is_enemy = (a->info->type == "enemy");
        bool is_player = (a->info->type == "Player");

        if (!is_enemy && !is_player) continue;

        double dist = Range::get_distance(new_pos, a);
        if (dist < 40.0) {
            return true; 
        }
    }

    return false;
}


std::string AnimationUpdate::pick_best_animation_towards(SDL_Point target) const {
	if (!self_ || !self_->info) return {};
	const auto& all = self_->info->animations;
	if (all.empty()) return {};
	double best_d = std::numeric_limits<double>::infinity();
	std::string best_id;
	for (const auto& kv : all) {
		const std::string& id = kv.first;
		const Animation& anim = kv.second;
		const int dx = anim.total_dx;
		const int dy = anim.total_dy;
		if (dx == 0 && dy == 0) continue;
		if (!can_move_by(dx, dy)) continue;
		if (would_overlap_same_or_player(dx, dy)) continue;
		SDL_Point next{ self_->pos.x + dx, self_->pos.y + dy };
		double d = Range::get_distance(next, target);
		if (d < best_d) {
			best_d = d;
			best_id = id;
		}
	}
	return best_id;
}


void AnimationUpdate::clamp_to_room(int& x, int& y) const {
	if (!confined_) return;
	if (!self_) return;
	Assets* assets = self_->get_assets();
	if (!assets || !assets->current_room_ || !assets->current_room_->room_area) return;
	auto [minx, miny, maxx, maxy] = assets->current_room_->room_area->get_bounds();
	if (x < minx) x = minx;
	if (x > maxx) x = maxx;
	if (y < miny) y = miny;
	if (y > maxy) y = maxy;
}

int AnimationUpdate::min_move_len2() const {
        if (cached_min_move_len2_ >= 0) return cached_min_move_len2_;
        cached_min_move_len2_ = std::numeric_limits<int>::max();
        if (!self_ || !self_->info) { cached_min_move_len2_ = 1; return cached_min_move_len2_; }
        for (const auto& kv : self_->info->animations) {
                const Animation& anim = kv.second;
                SDL_Point delta{anim.total_dx, anim.total_dy};
                double len = Range::get_distance(SDL_Point{0,0}, delta);
                int len2 = static_cast<int>(len * len);
                if (anim.total_dx == 0 && anim.total_dy == 0) continue;
                if (len2 > 0) cached_min_move_len2_ = std::min(cached_min_move_len2_, len2);
        }
        if (cached_min_move_len2_ == std::numeric_limits<int>::max()) cached_min_move_len2_ = 1;
        return cached_min_move_len2_;
}

bool AnimationUpdate::is_target_reached() {
    if (!self_) return true;
    double d = Range::get_distance(SDL_Point{ self_->pos.x, self_->pos.y }, target_);
    bool reached = d <= std::sqrt(static_cast<double>(min_move_len2()));

    if (reached) {
        switch (mode_) {
            case Mode::Idle:
                ensure_idle_target(idle_min_dist_, idle_max_dist_);
                break;
            case Mode::Pursue:
                ensure_pursue_target(pursue_min_dist_, pursue_max_dist_, pursue_target_);
                break;
            case Mode::Run:
                ensure_run_target(run_min_dist_, run_max_dist_, run_threat_);
                break;
            case Mode::Orbit:
                ensure_orbit_target(orbit_min_radius_, orbit_max_radius_, orbit_center_, orbit_keep_ratio_);
                break;
            case Mode::Patrol:
                ensure_patrol_target(patrol_points_, patrol_loop_, patrol_hold_frames_);
                break;
            case Mode::Serpentine:
                ensure_serpentine_target(serp_min_stride_, serp_max_stride_, serp_sway_, serp_target_, serp_keep_ratio_);
                break;
            default:
                break;
        }
    }

    return reached;
}


void AnimationUpdate::ensure_idle_target(int min_dist, int max_dist) {
	if (mode_ == Mode::Idle && have_target_ && !is_target_reached()) return;
	int cx = self_ ? self_->pos.x : 0;
	int cy = self_ ? self_->pos.y : 0;
	if (max_dist < min_dist) std::swap(min_dist, max_dist);
	min_dist = std::max(0, min_dist);
	max_dist = std::max(0, max_dist);
	std::uniform_real_distribution<double> angle(0.0, 2.0 * 3.14159265358979323846);
	std::uniform_real_distribution<double> radius(static_cast<double>(min_dist), static_cast<double>(max_dist));
	double a = angle(rng_);
	double r = radius(rng_);
	int tx = cx + static_cast<int>(std::llround(r * std::cos(a)));
	int ty = cy + static_cast<int>(std::llround(r * std::sin(a)));
	set_target(SDL_Point{tx, ty}, nullptr);
	mode_ = Mode::Idle;
}

void AnimationUpdate::ensure_pursue_target(int min_dist, int max_dist, const Asset* final_target) {
	if (mode_ == Mode::Pursue && have_target_ && !is_target_reached()) return;
	if (!self_ || !final_target) return;
	if (max_dist < min_dist) std::swap(min_dist, max_dist);
	min_dist = std::max(0, min_dist);
	max_dist = std::max(0, max_dist);
	int cx = self_->pos.x;
	int cy = self_->pos.y;
	int tx = final_target->pos.x;
	int ty = final_target->pos.y;
	int vx = tx - cx;
	int vy = ty - cy;
	double a = std::atan2(static_cast<double>(vy), static_cast<double>(vx));
	std::uniform_real_distribution<double> radius(static_cast<double>(min_dist), static_cast<double>(max_dist));
	double r = radius(rng_);
	int nx = cx + static_cast<int>(std::llround(r * std::cos(a)));
	int ny = cy + static_cast<int>(std::llround(r * std::sin(a)));
	set_target(SDL_Point{nx, ny}, final_target);
	mode_ = Mode::Pursue;
}

void AnimationUpdate::ensure_run_target(int min_dist, int max_dist, const Asset* threat) {
	if (mode_ == Mode::Run && have_target_ && !is_target_reached()) return;
	if (!self_ || !threat) return;
	if (max_dist < min_dist) std::swap(min_dist, max_dist);
	min_dist = std::max(0, min_dist);
	max_dist = std::max(0, max_dist);
	int cx = self_->pos.x;
	int cy = self_->pos.y;
	int tx = threat->pos.x;
	int ty = threat->pos.y;
	int vx = cx - tx;
	int vy = cy - ty;
	double a = std::atan2(static_cast<double>(vy), static_cast<double>(vx));
	if (vx == 0 && vy == 0) {
		std::uniform_real_distribution<double> ang(0.0, 2.0 * 3.14159265358979323846);
		a = ang(rng_);
	}
	std::uniform_real_distribution<double> radius(static_cast<double>(min_dist), static_cast<double>(max_dist));
	double r = radius(rng_);
	int nx = cx + static_cast<int>(std::llround(r * std::cos(a)));
	int ny = cy + static_cast<int>(std::llround(r * std::sin(a)));
	set_target(SDL_Point{nx, ny}, nullptr);
	mode_ = Mode::Run;
}

void AnimationUpdate::ensure_orbit_target(int min_radius, int max_radius, const Asset* center, int keep_direction_ratio) {
	if (mode_ == Mode::Orbit && have_target_ && !is_target_reached()) return;
	if (!self_ || !center) return;
	if (max_radius < min_radius) std::swap(min_radius, max_radius);
	min_radius = std::max(0, min_radius);
	max_radius = std::max(0, max_radius);
	if (mode_ == Mode::Orbit && orbit_params_set_) {
		int denom = std::max(0, keep_direction_ratio) + 1;
		std::uniform_int_distribution<int> pick(0, denom - 1);
		bool keep = (pick(rng_) != 0);
		if (!keep) orbit_dir_ = -orbit_dir_;
	} else {
		std::uniform_int_distribution<int> dirpick(0, 1);
		orbit_dir_ = dirpick(rng_) ? +1 : -1;
	}
	if (!orbit_params_set_) {
		std::uniform_int_distribution<int> rpick(min_radius, max_radius);
		orbit_radius_ = rpick(rng_);
	} else {
		orbit_radius_ = std::clamp(orbit_radius_, min_radius, max_radius);
	}
	const int cx = center->pos.x;
	const int cy = center->pos.y;
	int vx = self_->pos.x - cx;
	int vy = self_->pos.y - cy;
	if (!orbit_params_set_) {
		if (vx == 0 && vy == 0) {
			std::uniform_real_distribution<double> ang(0.0, 2.0 * 3.14159265358979323846);
			orbit_angle_ = ang(rng_);
		} else {
			orbit_angle_ = std::atan2(static_cast<double>(vy), static_cast<double>(vx));
		}
		orbit_params_set_ = true;
	}
	double step_len = std::max(1.0, std::sqrt(static_cast<double>(min_move_len2())));
	double dtheta = step_len / std::max(1, orbit_radius_);
	if (dtheta < 0.08) dtheta = 0.08;
	double next_angle = orbit_angle_ + static_cast<double>(orbit_dir_) * dtheta;
	int nx = cx + static_cast<int>(std::llround(std::cos(next_angle) * orbit_radius_));
	int ny = cy + static_cast<int>(std::llround(std::sin(next_angle) * orbit_radius_));
	set_target(SDL_Point{nx, ny}, nullptr);
	mode_ = Mode::Orbit;
	orbit_angle_ = next_angle;
}

void AnimationUpdate::ensure_patrol_target(const std::vector<SDL_Point>& waypoints,
                                        bool loop,
                                        int hold_frames)
{
	if (!self_) return;
	if (waypoints.empty()) return;
	if (mode_ == Mode::Patrol && have_target_ && !is_target_reached()) return;
	if (mode_ == Mode::Patrol && have_target_ && is_target_reached()) {
		if (patrol_hold_left_ > 0) {
			patrol_hold_left_ -= 1;
			return;
		}
	}
	if (!patrol_initialized_ || patrol_points_.size() != waypoints.size()) {
		patrol_points_ = waypoints;
		patrol_index_ = 0;
		patrol_loop_ = loop;
		patrol_hold_frames_ = std::max(0, hold_frames);
		patrol_hold_left_ = patrol_hold_frames_;
		patrol_initialized_ = true;
	}
	if (mode_ == Mode::Patrol && have_target_ && is_target_reached()) {
		if (patrol_loop_) {
			patrol_index_ = (patrol_index_ + 1) % patrol_points_.size();
		} else if (patrol_index_ + 1 < patrol_points_.size()) {
			patrol_index_ += 1;
		}
		patrol_hold_left_ = patrol_hold_frames_;
	}
	SDL_Point wp = patrol_points_[patrol_index_];
	int nx = wp.x;
	int ny = wp.y;
	set_target(SDL_Point{nx, ny}, nullptr);
	mode_ = Mode::Patrol;
}

void AnimationUpdate::ensure_serpentine_target(int min_stride,
                                            int max_stride,
                                            int sway,
                                            const Asset* final_target,
                                            int keep_side_ratio)
{
	if (!self_ || !final_target) return;
	if (mode_ == Mode::Serpentine && have_target_ && !is_target_reached()) return;
	if (max_stride < min_stride) std::swap(min_stride, max_stride);
	min_stride = std::max(0, min_stride);
	max_stride = std::max(0, max_stride);
	sway = std::max(0, sway);
	int cx = self_->pos.x;
	int cy = self_->pos.y;
	int tx = final_target->pos.x;
	int ty = final_target->pos.y;
	int vx = tx - cx;
	int vy = ty - cy;
	double a;
	if (vx == 0 && vy == 0) {
		std::uniform_real_distribution<double> ang(0.0, 2.0 * 3.14159265358979323846);
		a = ang(rng_);
	} else {
		a = std::atan2(static_cast<double>(vy), static_cast<double>(vx));
	}
	if (mode_ == Mode::Serpentine && serp_params_set_) {
		int denom = std::max(0, keep_side_ratio) + 1;
		std::uniform_int_distribution<int> pick(0, denom - 1);
		bool keep = (pick(rng_) != 0);
		if (!keep) serp_side_ = -serp_side_;
	} else {
		std::uniform_int_distribution<int> sidepick(0, 1);
		serp_side_ = sidepick(rng_) ? +1 : -1;
	}
	std::uniform_int_distribution<int> stridepick(min_stride, max_stride);
	serp_stride_ = stridepick(rng_);
	double bx = static_cast<double>(cx) + static_cast<double>(serp_stride_) * std::cos(a);
	double by = static_cast<double>(cy) + static_cast<double>(serp_stride_) * std::sin(a);
	double pvx, pvy;
	if (vx == 0 && vy == 0) {
		pvx = -std::sin(a);
		pvy =  std::cos(a);
	} else {
		double norm = std::sqrt(static_cast<double>(vx) * vx + static_cast<double>(vy) * vy);
		pvx = -static_cast<double>(vy) / norm;
		pvy =  static_cast<double>(vx) / norm;
	}
	double ox = bx + static_cast<double>(serp_side_) * static_cast<double>(sway) * pvx;
	double oy = by + static_cast<double>(serp_side_) * static_cast<double>(sway) * pvy;
	int nx = static_cast<int>(std::llround(ox));
	int ny = static_cast<int>(std::llround(oy));
	set_target(SDL_Point{nx, ny}, final_target);
        mode_ = Mode::Serpentine;
        serp_params_set_ = true;
}

void AnimationUpdate::update() {
    if (!self_ || !self_->info) return;

    if (!self_->next_animation.empty()) {
        const std::string next = self_->next_animation;

        if (next == self_->current_animation) {
            auto it = self_->info->animations.find(self_->current_animation);
            if (it != self_->info->animations.end()) {
                Animation& anim = it->second;
                anim.change(self_->current_frame_index, self_->static_frame);
                self_->frame_progress = 0.0f;
                if ((anim.randomize || anim.rnd_start) && anim.frames.size() > 1) {
                    std::mt19937 g{ std::random_device{}() };
                    std::uniform_int_distribution<int> d(0, int(anim.frames.size()) - 1);
                    self_->current_frame_index = d(g);
                }
            }
            self_->next_animation.clear();
        } else if (next == "end") {
            std::cout << "End called for " << (self_->info ? self_->info->name : std::string("<unknown>")) << "\n";
            self_->next_animation.clear();
            self_->Delete();
            return;
        } else if (next == "freeze_on_last") {
            auto it = self_->info->animations.find(self_->current_animation);
            if (it != self_->info->animations.end()) {
                const Animation& curr = it->second;
                const int last = static_cast<int>(curr.frames.size()) - 1;
                if (self_->current_frame_index >= last) {
                    self_->static_frame = true;
                    self_->next_animation.clear();
                    return;
                }
            }
        } else {
            auto nit = self_->info->animations.find(next);
            if (nit != self_->info->animations.end()) {
                self_->current_animation = next;
                Animation& anim = nit->second;
                anim.change(self_->current_frame_index, self_->static_frame);
                self_->frame_progress = 0.0f;
                if ((anim.randomize || anim.rnd_start) && anim.frames.size() > 1) {
                    std::mt19937 g{ std::random_device{}() };
                    std::uniform_int_distribution<int> d(0, int(anim.frames.size()) - 1);
                    self_->current_frame_index = d(g);
                }
            }
            self_->next_animation.clear();
        }
    }

    auto it = self_->info->animations.find(self_->current_animation);
    if (it == self_->info->animations.end()) return;

    Animation& anim = it->second;

    if (self_->current_frame_index < 0 ||
        self_->current_frame_index >= anim.number_of_frames) {
        if (anim.number_of_frames > 0)
            self_->current_frame_index = std::clamp(self_->current_frame_index, 0, anim.number_of_frames - 1);
        else
            self_->current_frame_index = 0;
    }

    if (self_->static_frame) {
        if (self_->next_animation.empty() && !anim.on_end_animation.empty()) {
            self_->next_animation = anim.on_end_animation;
        }
        return;
    }

    int dx = 0;
    int dy = 0;
    bool resort_z = false;

    const bool advanced = anim.advance(self_->current_frame_index,
                                       self_->frame_progress,
                                       dx, dy, resort_z);

    self_->pos.x += dx;
    self_->pos.y += dy;

    if (!advanced && !anim.loop && self_->next_animation.empty() &&
        !anim.on_end_animation.empty()) {
        self_->next_animation = anim.on_end_animation;
    }

    if ((dx != 0 || dy != 0) && resort_z) {
        self_->set_z_index();
        if (Assets* as = self_->get_assets()) {
            as->activeManager.sortByZIndex();
        }
    }
}
