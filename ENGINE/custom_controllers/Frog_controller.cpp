#include "Frog_controller.hpp"
#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "custom_controllers/controller_path_utils.hpp"
#include "custom_controllers/controller_visit_threshold.hpp"
#include "utils/range_util.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <unordered_set>
#include <vector>

namespace {

constexpr double kThreatDetectionRadius = 200.0;
constexpr Uint32 kIdleHopIntervalMinMs  = 1000;
constexpr Uint32 kIdleHopIntervalMaxMs  = 10000;
constexpr Uint32 kRunHopIntervalMinMs   = 350;
constexpr Uint32 kRunHopIntervalMaxMs   = 850;
constexpr double kPi                    = 3.14159265358979323846;
constexpr double kTau                   = 6.28318530717958647692;
constexpr double kFleeAngleJitter       = kPi / 6.0;
constexpr double kFleeDistanceScaleMin  = 0.65;
constexpr double kFleeDistanceScaleMax  = 1.0;

}

FrogController::FrogController(Assets* assets, Asset* self)
    : assets_(assets), self_(self), rng_(std::random_device{}()) {
    if (self_ && self_->anim_) {
        enter_idle(idle_ratio_);
    }
}

void FrogController::enter_idle(int rest_ratio) {
    if (!self_ || !self_->anim_) {
        return;
    }

    idle_ratio_ = std::clamp(rest_ratio, 0, 100);
    state_ = State::Idle;
    last_run_target_ = nullptr;
    next_run_hop_time_ms_ = 0;

    self_->anim_->move({}, controller_utils::controller_visit_threshold(self_));
    self_->anim_->path_requested = true;
    schedule_next_idle_hop();
}

void FrogController::enter_run() {
    if (!self_ || !self_->anim_) {
        return;
    }

    state_ = State::Running;
    last_run_target_ = nullptr;
    next_run_hop_time_ms_ = SDL_GetTicks();
    self_->anim_->path_requested = true;
}

void FrogController::update(const Input&) {
    if (!self_ || !self_->anim_) {
        return;
    }

    if (!assets_ || !self_->info) {
        enter_idle(5);
        return;
    }

    try {
        Asset* threat = find_nearest_moving_threat(kThreatDetectionRadius);
        if (threat) {
            const bool previously_had_threat = threat_in_range_last_tick_;
            threat_in_range_last_tick_        = true;

            bool interrupt_rest_or_jump = !previously_had_threat;

            if (state_ != State::Running) {
                enter_run();
                interrupt_rest_or_jump = true;
            }

            if (threat != last_run_target_) {
                last_run_target_       = threat;
                interrupt_rest_or_jump = true;
            }

            const Uint32 now = SDL_GetTicks();
            if (interrupt_rest_or_jump) {
                self_->anim_->path_requested = true;
                next_run_hop_time_ms_        = now;
            }

            if (self_->anim_->path_requested && now >= next_run_hop_time_ms_) {
                perform_run_hop(last_run_target_);
            }
            return;
        }

        threat_in_range_last_tick_ = false;

        if (state_ != State::Idle) {
            enter_idle(idle_ratio_);
        }

        const Uint32 now = SDL_GetTicks();
        if (self_->anim_->path_requested && now >= next_idle_hop_time_ms_) {
            perform_idle_hop();
        }
    } catch (...) {
        enter_idle(35);
        threat_in_range_last_tick_ = false;
    }
}

void FrogController::schedule_next_idle_hop() {
    const Uint32 now = SDL_GetTicks();
    std::uniform_int_distribution<int> wait_dist(static_cast<int>(kIdleHopIntervalMinMs), static_cast<int>(kIdleHopIntervalMaxMs));
    next_idle_hop_time_ms_ = now + static_cast<Uint32>(wait_dist(rng_));
}

void FrogController::perform_idle_hop() {
    if (!self_ || !self_->anim_) {
        return;
    }

    const SDL_Point destination = random_idle_destination();
    if (destination.x == self_->pos.x && destination.y == self_->pos.y) {
        schedule_next_idle_hop();
        return;
    }

    std::vector<SDL_Point> path;
    path.push_back(SDL_Point{ destination.x - self_->pos.x, destination.y - self_->pos.y });
    self_->anim_->move(path, controller_utils::controller_visit_threshold(self_));
    schedule_next_idle_hop();
}

void FrogController::schedule_next_run_hop() {
    const Uint32 now = SDL_GetTicks();
    std::uniform_int_distribution<int> wait_dist(static_cast<int>(kRunHopIntervalMinMs), static_cast<int>(kRunHopIntervalMaxMs));
    next_run_hop_time_ms_ = now + static_cast<Uint32>(wait_dist(rng_));
}

void FrogController::perform_run_hop(Asset* threat) {
    if (!self_ || !self_->anim_) {
        return;
    }

    const auto destination = flee_destination(threat);
    std::vector<SDL_Point> path;
    if (destination.x != self_->pos.x || destination.y != self_->pos.y) {
        path.push_back(SDL_Point{ destination.x - self_->pos.x, destination.y - self_->pos.y });
    }

    if (path.empty()) {
        const auto fallback = controller_paths::flee_path(self_, threat);
        self_->anim_->move(fallback, controller_utils::controller_visit_threshold(self_));
    } else {
        self_->anim_->move(path, controller_utils::controller_visit_threshold(self_));
    }

    schedule_next_run_hop();
}

Asset* FrogController::find_nearest_moving_threat(double radius) {
    if (!assets_ || !self_) {
        return nullptr;
    }

    const auto& candidates = assets_->getFilteredActiveAssets();
    if (candidates.empty()) {
        last_known_positions_.clear();
        return nullptr;
    }

    std::unordered_set<Asset*> seen;
    seen.reserve(candidates.size());

    Asset*  closest      = nullptr;
    double  best_distance = radius;

    for (Asset* candidate : candidates) {
        if (!candidate || candidate == self_ || candidate->dead || !candidate->active) {
            if (candidate) {
                last_known_positions_.erase(candidate);
            }
            continue;
        }

        seen.insert(candidate);

        const bool is_player = (assets_->player && candidate == assets_->player);
        if (!is_asset_moving(candidate) && !is_player) {
            continue;
        }

        const double distance = Range::get_distance(self_, candidate);
        if (distance <= radius && (!closest || distance < best_distance)) {
            best_distance = distance;
            closest       = candidate;
        }
    }

    prune_stale_positions(seen);
    return closest;
}

bool FrogController::is_asset_moving(Asset* candidate) {
    if (!candidate || !candidate->anim_) {
        last_known_positions_.erase(candidate);
        return false;
    }

    SDL_Point current_pos = candidate->pos;
    bool      moving      = false;

    auto it = last_known_positions_.find(candidate);
    if (it != last_known_positions_.end()) {
        moving = (it->second.x != current_pos.x || it->second.y != current_pos.y);
        it->second = current_pos;
    } else {
        last_known_positions_.emplace(candidate, current_pos);
    }

    if (!moving) {
        const SDL_Point dest = candidate->anim_->final_dest;
        moving = (dest.x != current_pos.x || dest.y != current_pos.y);
    }

    return moving;
}

void FrogController::prune_stale_positions(const std::unordered_set<Asset*>& seen) {
    for (auto it = last_known_positions_.begin(); it != last_known_positions_.end();) {
        if (seen.find(it->first) == seen.end()) {
            it = last_known_positions_.erase(it);
        } else {
            ++it;
        }
    }
}

SDL_Point FrogController::random_idle_destination() {
    if (!self_) {
        return SDL_Point{ 0, 0 };
    }

    const SDL_Point origin = self_->pos;
    const int       radius = controller_paths::neighbor_radius(self_);
    if (radius <= 0) {
        return origin;
    }

    std::uniform_real_distribution<double> angle_dist(0.0, kTau);
    std::uniform_real_distribution<double> distance_dist(static_cast<double>(radius) * 0.25, static_cast<double>(radius));

    for (int attempt = 0; attempt < 8; ++attempt) {
        const double angle = angle_dist(rng_);
        const double dist  = distance_dist(rng_);
        SDL_Point desired{ origin.x + static_cast<int>(std::round(std::cos(angle) * dist)),
                           origin.y + static_cast<int>(std::round(std::sin(angle) * dist)) };
        SDL_Point clamped = controller_paths::clamp_to_radius(origin, desired, radius);
        if (clamped.x != origin.x || clamped.y != origin.y) {
            return clamped;
        }
    }

    SDL_Point fallback{ origin.x + 1, origin.y };
    return controller_paths::clamp_to_radius(origin, fallback, radius);
}

SDL_Point FrogController::flee_destination(Asset* threat) {
    if (!self_) {
        return SDL_Point{ 0, 0 };
    }

    const SDL_Point origin = self_->pos;
    const int       radius = controller_paths::neighbor_radius(self_);
    if (radius <= 0) {
        return origin;
    }

    SDL_Point away{ 1, 0 };
    if (threat) {
        away.x = origin.x - threat->pos.x;
        away.y = origin.y - threat->pos.y;
        if (away.x == 0 && away.y == 0) {
            away.x = 1;
        }
    }

    const double base_angle = std::atan2(static_cast<double>(away.y), static_cast<double>(away.x));
    std::uniform_real_distribution<double> angle_jitter(-kFleeAngleJitter, kFleeAngleJitter);
    std::uniform_real_distribution<double> distance_scale(kFleeDistanceScaleMin, kFleeDistanceScaleMax);

    const double angle        = base_angle + angle_jitter(rng_);
    const double scale        = distance_scale(rng_);
    const double desired_dist = static_cast<double>(radius) * scale;

    SDL_Point desired{ origin.x + static_cast<int>(std::round(std::cos(angle) * desired_dist)),
                       origin.y + static_cast<int>(std::round(std::sin(angle) * desired_dist)) };

    return controller_paths::clamp_to_radius(origin, desired, radius);
}
