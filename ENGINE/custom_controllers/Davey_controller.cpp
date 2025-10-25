#include "Davey_controller.hpp"
#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "custom_controllers/controller_path_utils.hpp"
#include "custom_controllers/controller_visit_threshold.hpp"
#include "utils/range_util.hpp"

#include <algorithm>

namespace {

constexpr int kOrbitSteps = 8;

}

DaveyController::DaveyController(Assets* assets, Asset* self)
    : assets_(assets), self_(self) {
    if (self_ && self_->anim_) {
        enter_idle(idle_ratio_);
    }
}

void DaveyController::enter_idle(int rest_ratio) {
    if (!self_ || !self_->anim_) {
        return;
    }

    idle_ratio_ = std::clamp(rest_ratio, 0, 100);
    state_           = State::Idle;
    current_target_  = nullptr;
    orbit_radius_    = 0;

    const auto path = controller_paths::idle_path(self_, idle_ratio_);
    self_->anim_->move(path, controller_utils::controller_visit_threshold(self_, path));
}

void DaveyController::enter_pursue(Asset* target) {
    if (!self_ || !self_->anim_) {
        return;
    }
    if (!target) {
        enter_idle(idle_ratio_);
        return;
    }

    state_           = State::Pursuing;
    current_target_  = target;
    orbit_radius_    = 0;

    const auto path = controller_paths::pursue_path(self_, target);
    self_->anim_->move(path, controller_utils::controller_visit_threshold(self_, path));
}

void DaveyController::enter_orbit(Asset* center, int radius) {
    if (!self_ || !self_->anim_) {
        return;
    }
    if (!center) {
        enter_idle(idle_ratio_);
        return;
    }

    state_           = State::Orbiting;
    current_target_  = center;
    orbit_radius_    = std::max(0, radius);

    const auto path = controller_paths::orbit_path(self_, center, radius, kOrbitSteps);
    if (path.empty()) {
        enter_pursue(center);
        return;
    }

    self_->anim_->move(path, controller_utils::controller_visit_threshold(self_, path));
}

void DaveyController::update(const Input&) {
    if (!self_ || !self_->anim_) {
        return;
    }

    auto target_active = [](Asset* asset) {
        return asset && !asset->dead && asset->active;
    };

    if (self_->anim_->path_requested) {
        switch (state_) {
        case State::Idle:
            enter_idle(idle_ratio_);
            break;
        case State::Pursuing:
            if (target_active(current_target_)) {
                enter_pursue(current_target_);
            } else {
                enter_idle(idle_ratio_);
            }
            break;
        case State::Orbiting:
            if (target_active(current_target_) && orbit_radius_ > 0) {
                enter_orbit(current_target_, orbit_radius_);
            } else if (target_active(current_target_)) {
                enter_pursue(current_target_);
            } else {
                enter_idle(idle_ratio_);
            }
            break;
        }
    }

    if (!assets_ || !self_->info) {
        enter_idle(5);
        return;
    }

    try {
        Asset* player = assets_->player;
        if (!target_active(player) || player == self_) {
            enter_idle(10);
            return;
        }

        const double distance = Range::get_distance(self_, player);
        constexpr double orbit_radius  = 44.0;
        constexpr double chase_trigger = 360.0;

        if (distance <= orbit_radius) {
            enter_orbit(player, static_cast<int>(orbit_radius));
        } else if (distance <= chase_trigger) {
            enter_pursue(player);
        } else {
            enter_idle(10);
        }
    } catch (...) {
        enter_idle(5);
    }
}
