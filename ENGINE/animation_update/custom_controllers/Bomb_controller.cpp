#include "Bomb_controller.hpp"
#include "asset/Asset.hpp"
#include "asset/asset_types.hpp"
#include "core/AssetsManager.hpp"
#include "animation_update/custom_controllers/controller_path_utils.hpp"
#include "animation_update/custom_controllers/controller_visit_threshold.hpp"
#include "utils/log.hpp"
#include "utils/range_util.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string describe_asset(const Asset* asset) {
    if (!asset || !asset->info) {
        return "<null>";
    }
    return asset->info->name;
}

} // namespace

BombController::BombController(Assets* assets, Asset* self)
    : assets_(assets), self_(self) {
    if (self_ && self_->anim_) {
        self_->anim_->set_debug_enabled(true);
        vibble::log::info("[BombController] initialized for " + describe_asset(self_));
    }
}

bool BombController::target_active(Asset* asset) {
    return asset && !asset->dead && asset->active;
}

Asset* BombController::resolve_player_target() const {
    if (!assets_) {
        return nullptr;
    }

    Asset* player = assets_->player;
    if (target_active(player)) {
        return player;
    }

    const auto& active_assets = assets_->getActive();
    if (active_assets.empty()) {
        return nullptr;
    }

    Asset*  closest       = nullptr;
    double  best_distance = std::numeric_limits<double>::infinity();

    for (Asset* candidate : active_assets) {
        if (!target_active(candidate) || !candidate->info) {
            continue;
        }
        const std::string canonical_type = asset_types::canonicalize(candidate->info->type);
        if (canonical_type != asset_types::player) {
            continue;
        }

        const double distance = Range::get_distance(self_, candidate);
        if (!std::isfinite(distance)) {
            continue;
        }
        if (!closest || distance < best_distance) {
            closest       = candidate;
            best_distance = distance;
        }
    }

    return closest;
}

void BombController::enter_pursue(Asset* target) {
    if (!self_ || !self_->anim_) {
        return;
    }
    if (!target) {
        self_->anim_->clear_movement_plan();
        state_ = State::Idle;
        current_target_ = nullptr;
        return;
    }

    state_ = State::Pursuing;
    current_target_ = target;

    const auto path = controller_paths::pursue_path(self_, target);
    const int visit_thresh = controller_utils::controller_visit_threshold(self_, path);
    if (self_->anim_->debug_enabled()) {
        std::ostringstream oss;
        oss << "[BombController] enter_pursue target=" << describe_asset(target)
            << " path_size=" << path.size()
            << " visit_thresh=" << visit_thresh;
        if (!path.empty()) {
            const SDL_Point& step = path.back();
            oss << " final_offset=(" << step.x << "," << step.y << ")";
        }
        vibble::log::info(oss.str());
    }

    self_->anim_->auto_move(path, visit_thresh);
}

void BombController::update(const Input&) {
    if (!self_ || !self_->anim_) return;

    const bool debug = self_->anim_->debug_enabled();
    if (debug) {
        std::ostringstream oss;
        oss << "[BombController] update state=" << (state_ == State::Idle ? "Idle" : "Pursuing")
            << " current_target=" << describe_asset(current_target_);
        vibble::log::info(oss.str());
    }

    Asset* player = nullptr;
    try {
        player = resolve_player_target();
    } catch (...) {
        player = nullptr;
    }

    if (!target_active(player) || player == self_) {
        if (state_ != State::Idle) {
            if (debug) {
                vibble::log::info("[BombController] target dropped or inactive, returning to idle");
            }
            self_->anim_->clear_movement_plan();
            state_ = State::Idle;
            current_target_ = nullptr;
        }
        return;
    }

    const double distance = Range::get_distance(self_, player);
    if (debug) {
        std::ostringstream oss;
        oss << "[BombController] player target=" << describe_asset(player)
            << " distance=" << (std::isfinite(distance) ? std::to_string(distance) : std::string{"inf"})
            << " (ignoring radius)";
        vibble::log::info(oss.str());
    }

    if (debug) {
        vibble::log::info("[BombController] pursuing player");
    }
    enter_pursue(player);
}
