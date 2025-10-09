#include "LabBomb_controller.hpp"

#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "custom_controllers/controller_path_utils.hpp"
#include "custom_controllers/controller_visit_threshold.hpp"
#include "map_generation/room.hpp"
#include "utils/range_util.hpp"

#include <algorithm>
#include <cmath>
#include <string>

LabBombController::LabBombController(Assets* assets, Asset* self)
    : assets_(assets), self_(self) {
    if (self_ && self_->anim_) enter_idle(idle_ratio_);
}

void LabBombController::update(const Input&) {
    if (!self_ || spent_ || !self_->anim_) return;
    if (!assets_ || self_->owning_room_name() != "room_lab") { enter_idle(idle_ratio_); return; }
    const Room* room = assets_->current_room();
    if (!room || room->room_name != self_->owning_room_name()) { enter_idle(idle_ratio_); return; }
    Asset* player = assets_->player;
    if (!player || player == self_) { enter_idle(35); return; }

    if (!triggered_) {
        for (const Room::NamedArea* entry : assets_->current_room_trigger_areas()) {
            if (entry && entry->area && (entry->name == "attack_trigger" || entry->area->get_name() == "attack_trigger") && entry->area->contains_point(player->pos)) {
                triggered_ = true;
                break;
            }
        }
    }
    if (!triggered_) { enter_idle(idle_ratio_); return; }

    if (state_ != State::Pursuing || current_target_ != player) enter_pursue(player);

    constexpr int activation_radius = 5;
    const long long dist_sq = Range::distance_sq(self_, player);
    if (dist_sq > activation_radius * activation_radius) {
        const double dist = Range::get_distance(self_, player);
        if (!std::isfinite(dist) || dist > activation_radius) return;
    }

    if (!trigger_explosion()) self_->Delete();
    mark_spent();
}

void LabBombController::start_pursuit() {
    if (spent_ || !assets_ || !assets_->player || assets_->player == self_) { enter_idle(idle_ratio_); return; }
    triggered_ = true;
    enter_pursue(assets_->player);
}

void LabBombController::mark_spent() {
    spent_ = true;
    triggered_ = false;
    state_ = State::Detonated;
    current_target_ = nullptr;
}

void LabBombController::enter_idle(int rest_ratio) {
    if (!self_ || !self_->anim_ || state_ == State::Detonated) return;
    idle_ratio_ = std::clamp(rest_ratio, 0, 100);
    current_target_ = nullptr;
    state_ = State::Idle;
    self_->anim_->move(controller_paths::idle_path(self_, idle_ratio_), controller_utils::controller_visit_threshold(self_));
}

void LabBombController::enter_pursue(Asset* target) {
    if (!target || !self_ || !self_->anim_ || state_ == State::Detonated) { enter_idle(idle_ratio_); return; }
    state_ = State::Pursuing;
    current_target_ = target;
    self_->anim_->move(controller_paths::pursue_path(self_, target), 20);
}

bool LabBombController::trigger_explosion() {
    if (!self_ || !self_->anim_ || !self_->info) return false;
    const auto play = [&](const std::string& name) {
        const auto it = self_->info->animations.find(name);
        if (it == self_->info->animations.end()) return false;
        self_->anim_->set_animation_now(name);
        self_->anim_->move({}, controller_utils::controller_visit_threshold(self_));
        return true;
    };
    return play("explode") || play("explosion");
}
