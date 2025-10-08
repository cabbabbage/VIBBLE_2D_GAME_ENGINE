#include "LabBomb_controller.hpp"

#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "custom_controllers/controller_path_utils.hpp"
#include "custom_controllers/controller_visit_threshold.hpp"
#include "map_generation/room.hpp"
#include "utils/range_util.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

struct RoomState {
    explicit RoomState(std::string room)
        : room_name(std::move(room)), rng(std::random_device{}()) {}

    void register_bomb(LabBombController* ctrl) {
        if (!ctrl) return;
        cleanup();
        if (std::find(bombs.begin(), bombs.end(), ctrl) == bombs.end()) {
            bombs.push_back(ctrl);
            dirty_queue = true;
        }
        if (trigger_entered) {
            activate_next();
        }
    }

    void unregister_bomb(LabBombController* ctrl) {
        bombs.erase(std::remove(bombs.begin(), bombs.end(), ctrl), bombs.end());
        activation_queue.erase(std::remove(activation_queue.begin(), activation_queue.end(), ctrl), activation_queue.end());
        if (active_bomb == ctrl) {
            active_bomb = nullptr;
        }
        dirty_queue = true;
    }

    void mark_trigger_entered() {
        if (trigger_entered) {
            return;
        }
        trigger_entered = true;
        dirty_queue = true;
        activate_next();
    }

    void notify_bomb_spent(LabBombController* ctrl) {
        if (!ctrl) {
            return;
        }
        if (active_bomb == ctrl) {
            active_bomb = nullptr;
        }
        activation_queue.erase(std::remove(activation_queue.begin(), activation_queue.end(), ctrl), activation_queue.end());
        dirty_queue = true;
        activate_next();
    }

    bool is_active(const LabBombController* ctrl) const {
        return ctrl && active_bomb == ctrl;
    }

    bool is_triggered() const {
        return trigger_entered;
    }

    void activate_next() {
        if (!trigger_entered) {
            return;
        }
        cleanup();
        ensure_queue();
        if (active_bomb && !active_bomb->is_spent()) {
            return;
        }
        active_bomb = nullptr;
        while (!activation_queue.empty()) {
            LabBombController* candidate = activation_queue.back();
            activation_queue.pop_back();
            if (!candidate || candidate->is_spent()) {
                continue;
            }
            active_bomb = candidate;
            candidate->start_pursuit();
            break;
        }
    }

    void ensure_queue() {
        if (!dirty_queue) {
            return;
        }
        activation_queue.clear();
        for (LabBombController* bomb : bombs) {
            if (!bomb || bomb->is_spent()) {
                continue;
            }
            activation_queue.push_back(bomb);
        }
        std::shuffle(activation_queue.begin(), activation_queue.end(), rng);
        dirty_queue = false;
    }

    void cleanup() {
        bombs.erase(std::remove_if(bombs.begin(), bombs.end(), [](LabBombController* bomb) {
                         return bomb == nullptr || bomb->is_spent();
                     }),
                     bombs.end());
        if (!bombs.empty()) {
            return;
        }
        activation_queue.clear();
        active_bomb = nullptr;
        trigger_entered = false;
        dirty_queue = true;
    }

    std::string room_name;
    std::vector<LabBombController*> bombs;
    std::vector<LabBombController*> activation_queue;
    LabBombController* active_bomb = nullptr;
    bool trigger_entered = false;
    bool dirty_queue = true;
    std::mt19937 rng;
};

using RoomStateMap = std::unordered_map<std::string, RoomState>;

RoomStateMap& room_states() {
    static RoomStateMap states;
    return states;
}

RoomState* find_room_state(const std::string& room) {
    auto& states = room_states();
    auto it = states.find(room);
    if (it == states.end()) {
        return nullptr;
    }
    return &it->second;
}

RoomState& ensure_room_state(const std::string& room) {
    auto& states = room_states();
    auto [it, inserted] = states.try_emplace(room, room);
    if (inserted) {
        it->second.rng.seed(std::random_device{}());
    }
    return it->second;
}

void cleanup_room_state_if_empty(const std::string& room) {
    auto& states = room_states();
    auto it = states.find(room);
    if (it != states.end() && it->second.bombs.empty()) {
        states.erase(it);
    }
}

} // namespace

LabBombController::LabBombController(Assets* assets, Asset* self)
    : assets_(assets), self_(self) {
    if (self_ && self_->anim_) {
        enter_idle(idle_ratio_);
    }
}

LabBombController::~LabBombController() {
    if (!self_) {
        return;
    }
    const std::string& room_name = self_->owning_room_name();
    if (room_name.empty()) {
        return;
    }
    if (RoomState* state = find_room_state(room_name)) {
        state->unregister_bomb(this);
        cleanup_room_state_if_empty(room_name);
    }
}

bool LabBombController::ensure_registration() {
    if (registered_) {
        return true;
    }
    if (!self_ || self_->owning_room_name().empty()) {
        return false;
    }
    if (self_->owning_room_name() != "room_lab") {
        return false;
    }
    RoomState& state = ensure_room_state(self_->owning_room_name());
    state.register_bomb(this);
    registered_ = true;
    return true;
}

void LabBombController::enter_idle(int rest_ratio) {
    if (!self_ || !self_->anim_) {
        return;
    }
    idle_ratio_ = std::clamp(rest_ratio, 0, 100);
    current_target_ = nullptr;
    if (state_ == State::Detonated) {
        return;
    }
    state_ = State::Idle;
    const auto path = controller_paths::idle_path(self_, idle_ratio_);
    self_->anim_->move(path, controller_utils::controller_visit_threshold(self_));
}

void LabBombController::enter_pursue(Asset* target) {
    if (!self_ || !self_->anim_) {
        return;
    }
    if (!target || state_ == State::Detonated) {
        enter_idle(idle_ratio_);
        return;
    }
    state_ = State::Pursuing;
    current_target_ = target;
    const auto path = controller_paths::pursue_path(self_, target);
    self_->anim_->move(path, controller_utils::controller_visit_threshold(self_));
}

bool LabBombController::trigger_explosion() {
    if (!self_ || !self_->anim_) {
        return false;
    }
    if (state_ == State::Detonated) {
        return true;
    }
    state_ = State::Detonated;
    current_target_ = nullptr;

    const auto has_animation = [&](const std::string& name) {
        return self_->info && self_->info->animations.find(name) != self_->info->animations.end();
    };

    const auto play_animation = [&](const std::string& name) {
        self_->anim_->set_animation_now(name);
        self_->anim_->move({}, controller_utils::controller_visit_threshold(self_));
    };

    if (has_animation("explode")) {
        play_animation("explode");
        return true;
    }

    if (has_animation("explosion")) {
        play_animation("explosion");
        return true;
    }

    self_->Delete();
    return false;
}

bool LabBombController::is_in_owning_room() const {
    if (!assets_ || !self_) {
        return false;
    }
    const Room* current_room = assets_->current_room();
    if (!current_room) {
        return false;
    }
    return current_room->room_name == self_->owning_room_name();
}

bool LabBombController::is_player_inside_trigger() const {
    if (!assets_ || !self_) {
        return false;
    }
    Asset* player = assets_->player;
    if (!player) {
        return false;
    }
    const auto trigger_areas = assets_->current_room_trigger_areas();
    for (const Room::NamedArea* entry : trigger_areas) {
        if (!entry || !entry->area) {
            continue;
        }
        const std::string& candidate_name = entry->name;
        if (candidate_name == "attack_trigger" || entry->area->get_name() == "attack_trigger") {
            if (entry->area->contains_point(player->pos)) {
                return true;
            }
        }
    }
    return false;
}

void LabBombController::update(const Input&) {
    if (!self_ || !self_->anim_) {
        return;
    }
    if (spent_) {
        return;
    }
    if (!assets_) {
        enter_idle(5);
        return;
    }
    if (!ensure_registration()) {
        enter_idle(idle_ratio_);
        return;
    }
    RoomState* state = find_room_state(self_->owning_room_name());
    if (!state) {
        enter_idle(idle_ratio_);
        return;
    }
    if (!is_in_owning_room()) {
        enter_idle(idle_ratio_);
        return;
    }
    Asset* player = assets_->player;
    if (!player || player == self_) {
        enter_idle(35);
        return;
    }
    if (!state->is_triggered() && is_player_inside_trigger()) {
        state->mark_trigger_entered();
    }
    if (!state->is_active(this)) {
        enter_idle(idle_ratio_);
        return;
    }
    if (state_ != State::Pursuing || current_target_ != player) {
        enter_pursue(player);
    }
    constexpr int activation_radius = 50;
    const long long activation_radius_sq = static_cast<long long>(activation_radius) * activation_radius;
    const long long distance_sq = Range::distance_sq(self_, player);

    bool in_activation_range = distance_sq <= activation_radius_sq;
    if (!in_activation_range) {
        const double distance = Range::get_distance(self_, player);
        in_activation_range = std::isfinite(distance) && distance <= static_cast<double>(activation_radius);
    }

    if (in_activation_range) {
        const bool exploded = trigger_explosion();
        mark_spent();
        state->notify_bomb_spent(this);
        if (!exploded && self_ && !self_->dead) {
            self_->Delete();
        }
    }
}

void LabBombController::start_pursuit() {
    if (spent_ || !assets_) {
        return;
    }
    Asset* player = assets_->player;
    if (!player || player == self_) {
        enter_idle(idle_ratio_);
        return;
    }
    enter_pursue(player);
}

void LabBombController::mark_spent() {
    spent_ = true;
    state_ = State::Detonated;
    current_target_ = nullptr;
}

