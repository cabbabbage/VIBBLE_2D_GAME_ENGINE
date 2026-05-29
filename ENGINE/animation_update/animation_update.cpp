#include "animation_update.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "asset/Asset.hpp"
#include "asset/animation.hpp"
#include "asset/animation_frame.hpp"
#include "asset/asset_info.hpp"
#include "asset/asset_types.hpp"
#include "animation_runtime.hpp"
#include "core/AssetsManager.hpp"
#include "map_generation/room.hpp"
#include "utils/grid.hpp"
#include "utils/area.hpp"
#include "utils/log.hpp"

namespace {

struct PlayableRoomsCacheEntry {
    const Room* last_containing_room = nullptr;
    std::unordered_map<const Room*, bool> playable_lookup;
    std::uintptr_t rooms_identity = 0;
    std::size_t    rooms_size     = 0;
};

auto& playable_rooms_cache() {
    static std::unordered_map<const Assets*, PlayableRoomsCacheEntry> cache;
    return cache;
}

bool equals_ignore_case(std::string_view value, std::string_view target) {
    if (value.size() != target.size()) {
        return false;
    }
    for (std::size_t idx = 0; idx < value.size(); ++idx) {
        if (std::tolower(static_cast<unsigned char>(value[idx])) !=
            std::tolower(static_cast<unsigned char>(target[idx]))) {
            return false;
        }
    }
    return true;
}

bool compute_is_playable_room(const Room& room) {
    if (!room.room_area) {
        return false;
    }

    if (equals_ignore_case(room.room_area->get_type(), "room") ||
        equals_ignore_case(room.room_area->get_type(), "trail")) {
        return true;
    }

    return equals_ignore_case(room.type, "room") || equals_ignore_case(room.type, "trail");
}

bool is_playable_room_cached(const Room& room, PlayableRoomsCacheEntry& entry) {
    auto [it, inserted] = entry.playable_lookup.emplace(&room, false);
    if (inserted) {
        it->second = compute_is_playable_room(room);
    }
    return it->second;
}

}

namespace animation_update::detail {

bool should_consider_overlap(const Asset& self, const Asset& other) {
    if (!self.info || !other.info) {
        return false;
    }

    const std::string self_type  = asset_types::canonicalize(self.info->type);
    const std::string other_type = asset_types::canonicalize(other.info->type);

    if (self_type == asset_types::player || other_type == asset_types::player) {
        return false;
    }

    if (self.info->moving_asset && other.info->moving_asset) {
        return true;
    }

    if (other_type == asset_types::boundary) {
        return true;
    }

    if (other_type == asset_types::enemy || other_type == asset_types::npc) {
        return true;
    }

    if (self_type == other_type && other_type != asset_types::player) {
        return true;
    }

    return false;
}

int distance_sq(SDL_Point a, SDL_Point b) {
    const int dx = a.x - b.x;
    const int dy = a.y - b.y;
    return dx * dx + dy * dy;
}

bool segment_hits_area(SDL_Point from, SDL_Point to, const Area& area) {
    const int steps = std::max(std::abs(to.x - from.x), std::abs(to.y - from.y));
    if (steps == 0) {
        return area.contains_point(from);
    }

    const double step_x = (to.x - from.x) / static_cast<double>(steps);
    const double step_y = (to.y - from.y) / static_cast<double>(steps);

    for (int i = 0; i <= steps; ++i) {
        SDL_Point sample{ static_cast<int>(std::round(from.x + step_x * i)),
                          static_cast<int>(std::round(from.y + step_y * i)) };
        if (area.contains_point(sample)) {
            return true;
        }
    }
    return false;
}

SDL_Point bottom_middle_for(const Asset& asset, SDL_Point pos) {
    Area        area = asset.get_area("collision_area");
    const auto& pts  = area.get_points();
    if (pts.empty()) {
        return pos;
    }

    SDL_Point bottom = pts.front();
    for (const SDL_Point& pt : pts) {
        if (pt.y > bottom.y) {
            bottom = pt;
        }
    }

    const int offset_x = bottom.x - asset.pos.x;
    const int offset_y = bottom.y - asset.pos.y;
    return SDL_Point{ pos.x + offset_x, pos.y + offset_y };
}

SDL_Point frame_world_delta(const AnimationFrame& frame,
                            const Asset&          asset,
                            const vibble::grid::Grid& grid) {
    (void)asset;
    (void)grid;

    return SDL_Point{ frame.dx, frame.dy };
}

bool bottom_point_inside_playable_area(const Assets* assets, SDL_Point bottom_point) {
    if (!assets) {
        return false;
    }

    auto& cache_entry = playable_rooms_cache()[assets];

    const std::vector<Room*>& rooms = assets->rooms();
    const std::uintptr_t identity = rooms.empty() ? 0 : reinterpret_cast<std::uintptr_t>(rooms.data());
    if (cache_entry.rooms_identity != identity || cache_entry.rooms_size != rooms.size()) {
        cache_entry.rooms_identity        = identity;
        cache_entry.rooms_size            = rooms.size();
        cache_entry.last_containing_room  = nullptr;
        cache_entry.playable_lookup.clear();
    }

    auto contains_playable = [&](const Room* room) -> bool {
        if (!room || !room->room_area) {
            return false;
        }
        if (!is_playable_room_cached(*room, cache_entry)) {
            return false;
        }
        return room->room_area->contains_point(bottom_point);
};

    if (cache_entry.last_containing_room && contains_playable(cache_entry.last_containing_room)) {
        return true;
    }

    for (const Room* room : rooms) {
        if (contains_playable(room)) {
            cache_entry.last_containing_room = room;
            return true;
        }
    }

    cache_entry.last_containing_room = nullptr;
    return false;
}

bool segment_leaves_playable_area(const Assets* assets, SDL_Point from, SDL_Point to) {
    if (!assets) {
        return false;
    }

    const bool start_inside = bottom_point_inside_playable_area(assets, from);
    const bool end_inside   = bottom_point_inside_playable_area(assets, to);

    if (!start_inside || !end_inside) {
        return true;
    }

    const int steps = std::max(std::abs(to.x - from.x), std::abs(to.y - from.y));
    if (steps <= 1) {
        return false;
    }

    const double step_x = (to.x - from.x) / static_cast<double>(steps);
    const double step_y = (to.y - from.y) / static_cast<double>(steps);

    for (int i = 1; i < steps; ++i) {
        SDL_Point sample{ static_cast<int>(std::round(from.x + step_x * i)),
                          static_cast<int>(std::round(from.y + step_y * i)) };
        if (!bottom_point_inside_playable_area(assets, sample)) {
            return true;
        }
    }

    return false;
}

}

AnimationUpdate::AnimationUpdate(Asset* self, Assets* assets)
    : self_(self), assets_owner_(assets), grid_service_(&vibble::grid::global_grid()) {
    if (!assets_owner_ && self_) {
        assets_owner_ = self_->get_assets();
    }
}

axis::WorldPos AnimationUpdate::current_world_position() const {
    return self_ ? axis::from_xz(self_->pos, 0) : axis::WorldPos{};
}

void AnimationUpdate::auto_move(axis::WorldPos rel_checkpoint,
                                int visited_thresh_px,
                                std::optional<int> checkpoint_resolution,
                                bool override_non_locked) {
    std::vector<axis::WorldPos> rel{ rel_checkpoint };
    auto_move(rel, visited_thresh_px, checkpoint_resolution, override_non_locked);
}

void AnimationUpdate::auto_move_to(axis::WorldPos target_world,
                                   int visited_thresh_px,
                                   bool override_non_locked) {
    if (!self_) {
        return;
    }
    self_->target_reached = false;
    const axis::WorldPos here = current_world_position();
    axis::WorldPos delta{ target_world.x - here.x, target_world.y - here.y, target_world.z - here.z };
    if (debug_enabled_) {
        const double dx = static_cast<double>(delta.x);
        const double dy = static_cast<double>(delta.y);
        const double dz = static_cast<double>(delta.z);
        std::ostringstream oss;
        oss << "[AnimationUpdate] 3d engagement intent asset="
            << (self_->info ? self_->info->name : std::string{"<unknown>"})
            << " distance3d=" << std::sqrt(dx * dx + dy * dy + dz * dz)
            << " target=(" << target_world.x << "," << target_world.y << "," << target_world.z << ")"
            << " delta=(" << delta.x << "," << delta.y << "," << delta.z << ")";
        vibble::log::info(oss.str());
    }
    if (delta.x == 0 && delta.y == 0 && delta.z == 0) {
        self_->target_reached = true;
        self_->needs_target = true;
        return;
    }
    auto_move(delta, visited_thresh_px, std::nullopt, override_non_locked);
}

void AnimationUpdate::auto_move(const std::vector<axis::WorldPos>& rel_checkpoints,
                                int visited_thresh_px,
                                std::optional<int> checkpoint_resolution,
                                bool override_non_locked) {
    if (!self_) {
        return;
    }
    const std::string asset_name = self_->info ? self_->info->name : std::string{"<unknown>"};
    const int resolution = effective_grid_resolution(checkpoint_resolution);
    visited_thresh_      = std::max(0, visited_thresh_px);
    if (resolution > 0) {
        const int step = vibble::grid::delta(resolution);
        if (step > 1 && visited_thresh_ > 0) {
            visited_thresh_ = ((visited_thresh_ + step - 1) / step) * step;
        }
    }
    const bool debug_logging = debug_enabled_;
    if (debug_logging) {
        std::ostringstream oss;
        oss << "[AnimationUpdate] auto_move asset=" << asset_name
            << " rel_checkpoints=" << rel_checkpoints.size() << " visited_thresh=" << visited_thresh_ << " override_non_locked=" << std::boolalpha << override_non_locked;
        vibble::log::info(oss.str());
    }

    std::vector<axis::WorldPos> absolute;
    absolute.reserve(rel_checkpoints.size());
    vibble::grid::Grid& grid_service = grid();
    SDL_Point           cursor_index = grid_service.world_to_index(self_->pos, resolution);
    int                 cursor_y = current_world_position().y;
    for (const axis::WorldPos& delta : rel_checkpoints) {
        SDL_Point delta_xz{ delta.x, delta.z };
        SDL_Point delta_indices = grid_service.convert_resolution(delta_xz, 0, resolution);
        cursor_index.x += delta_indices.x;
        cursor_index.y += delta_indices.y;
        cursor_y += delta.y;
        SDL_Point next_world = grid_service.index_to_world(cursor_index, resolution);
        absolute.push_back(axis::from_xz(next_world, cursor_y));
    }

    plan_      = planner_(*self_, sanitizer_.sanitize(*self_, absolute, visited_thresh_), visited_thresh_, grid());
    final_dest = plan_.final_dest;
    plan_.world_start = current_world_position();
    plan_.override_non_locked = override_non_locked;
    if (debug_logging) {
        std::ostringstream oss;
        oss << "[AnimationUpdate] auto_move plan asset=" << asset_name
            << " final_dest=(" << final_dest.x << "," << final_dest.y << "," << final_dest.z << ")"
            << " sanitized_points=" << plan_.sanitized_checkpoints.size() << " strides=" << plan_.strides.size();
        vibble::log::info(oss.str());
    }

    if (plan_.strides.empty()) {
        if (debug_logging) {
            vibble::log::info("[AnimationUpdate] auto_move plan produced no strides for asset=" + asset_name + " reason=empty_or_stuck_3d_plan");
        }
        if (self_) {
            self_->needs_target = true;
        }
        return;
    }

    if (runtime_) {
        runtime_->reset_plan_progress();
    }

    input_event_ = true;
    if (self_) {
        self_->needs_target = false;
    }
}

void AnimationUpdate::move(axis::WorldPos delta,
                           const std::string& animation,
                           bool               resort_z,
                           bool               override_non_locked) {
    if (!self_ || !self_->info) {
        return;
    }

    pending_move_.delta        = delta;
    pending_move_.animation_id = animation;
    pending_move_.resort_z     = resort_z;
    pending_move_.override_non_locked = override_non_locked;
    move_pending_              = true;
    input_event_               = true;
}

void AnimationUpdate::clear_movement_plan() {
    const std::string asset_name = self_ && self_->info ? self_->info->name : std::string{"<unknown>"};
    const bool debug_logging = debug_enabled_;
    plan_.strides.clear();
    plan_.sanitized_checkpoints.clear();
    plan_.final_dest = self_ ? current_world_position() : axis::WorldPos{};
    plan_.override_non_locked = true;
    final_dest       = plan_.final_dest;
    input_event_     = true;

    if (debug_logging) {
        std::ostringstream oss;
        oss << "[AnimationUpdate] clear_movement_plan asset=" << asset_name
            << " final_dest=(" << final_dest.x << "," << final_dest.y << "," << final_dest.z << ")";
        vibble::log::info(oss.str());
    }

    if (runtime_) {
        runtime_->reset_plan_progress();
    }
    if (self_) {
        self_->needs_target = true;
    }
}

void AnimationUpdate::cancel_all_movement() {
    clear_movement_plan();
    move(axis::WorldPos{}, animation_update::detail::kDefaultAnimation, true, true);
}

std::size_t AnimationUpdate::path_index_for(const std::string& anim_id) const {
    if (runtime_) {
        return runtime_->path_index_for(anim_id);
    }
    return 0;
}

AnimationUpdate::MoveRequest AnimationUpdate::consume_move_request() {
    move_pending_ = false;
    return pending_move_;
}

bool AnimationUpdate::consume_input_event() {
    const bool had = input_event_;
    input_event_ = false;
    return had;
}

void AnimationUpdate::run_async(const std::string& child_name) {
    if (child_name.empty()) {
        return;
    }
    bool dispatched = false;
    if (runtime_) {
        dispatched = runtime_->run_child_animation(child_name);
    }
    if (!dispatched) {
        pending_async_requests_.push_back(child_name);
    }
    input_event_ = true;
}

std::vector<std::string> AnimationUpdate::consume_async_requests() {
    std::vector<std::string> pending = std::move(pending_async_requests_);
    pending_async_requests_.clear();
    return pending;
}

void AnimationUpdate::set_debug_enabled(bool enabled) {
    debug_enabled_ = enabled;
    if (runtime_) {
        runtime_->set_debug_enabled(enabled);
    }
}

bool AnimationUpdate::debug_enabled() const {
    return debug_enabled_;
}

vibble::grid::Grid& AnimationUpdate::grid() const {
    if (grid_service_) {
        return *grid_service_;
    }
    return vibble::grid::global_grid();
}

int AnimationUpdate::effective_grid_resolution(std::optional<int> override_resolution) const {
    (void)override_resolution;

    return 0;
}

void AnimationUpdate::set_animation(const std::string& animation_id) {
    if (!self_ || !self_->info) return;
    auto it = self_->info->animations.find(animation_id);
    if (it == self_->info->animations.end()) return;
    const Animation& anim = it->second;
    player_.m_animation = const_cast<Animation*>(&anim);
}
