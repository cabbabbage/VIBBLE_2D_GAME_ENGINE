#include "animation_update.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
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
#include "util/grid.hpp"
#include "utils/area.hpp"

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

} // namespace

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
    // Interpret frame dx/dy primarily as world-space pixel deltas.
    // Only snap to grid when the delta is aligned to the grid step, to avoid
    // rounding small movements to zero at higher grid resolutions.
    int resolution = vibble::grid::clamp_resolution(asset.grid_resolution);
    try {
        if (asset.info && asset_types::canonicalize(asset.info->type) == asset_types::player) {
            resolution = 0; // player always moves pixel-by-pixel
        }
    } catch (...) {
        // if anything goes wrong, just use the clamped asset value
    }
    if (resolution <= 0) {
        return SDL_Point{ frame.dx, frame.dy };
    }

    const int step = vibble::grid::delta(resolution);
    if (step <= 1) {
        return SDL_Point{ frame.dx, frame.dy };
    }

    const bool aligned_x = vibble::grid::is_multiple_of_delta(frame.dx, resolution);
    const bool aligned_y = vibble::grid::is_multiple_of_delta(frame.dy, resolution);
    if (!aligned_x || !aligned_y) {
        // Keep sub-grid motion as-is in world space
        return SDL_Point{ frame.dx, frame.dy };
    }

    // Delta is aligned with the grid; convert via indices to preserve exact steps
    SDL_Point indices    = grid.convert_resolution(SDL_Point{ frame.dx, frame.dy }, 0, resolution);
    const SDL_Point origin = grid.index_to_world(SDL_Point{ 0, 0 }, resolution);
    const SDL_Point target = grid.index_to_world(indices, resolution);
    return SDL_Point{ target.x - origin.x, target.y - origin.y };
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

} // namespace animation_update::detail

AnimationUpdate::AnimationUpdate(Asset* self, Assets* assets)
    : self_(self), assets_owner_(assets), grid_service_(&vibble::grid::global_grid()) {
    if (!assets_owner_ && self_) {
        assets_owner_ = self_->get_assets();
    }
}

AnimationUpdate::AnimationUpdate(Asset* self, Assets* assets, double)
    : AnimationUpdate(self, assets) {}

void AnimationUpdate::auto_move(const std::vector<SDL_Point>& rel_checkpoints,
                                int visited_thresh_px,
                                std::optional<int> checkpoint_resolution,
                                bool override_non_locked) {
    if (!self_) {
        return;
    }
    const int resolution = effective_grid_resolution(checkpoint_resolution);
    visited_thresh_      = std::max(0, visited_thresh_px);
    if (resolution > 0) {
        const int step = vibble::grid::delta(resolution);
        if (step > 1 && visited_thresh_ > 0) {
            visited_thresh_ = ((visited_thresh_ + step - 1) / step) * step;
        }
    }
    path_requested = false;

    std::vector<SDL_Point> absolute;
    absolute.reserve(rel_checkpoints.size());
    vibble::grid::Grid& grid_service = grid();
    SDL_Point           cursor_index = grid_service.world_to_index(self_->pos, resolution);
    for (const SDL_Point& delta : rel_checkpoints) {
        SDL_Point delta_indices = grid_service.convert_resolution(delta, 0, resolution);
        cursor_index.x += delta_indices.x;
        cursor_index.y += delta_indices.y;
        SDL_Point next_world = grid_service.index_to_world(cursor_index, resolution);
        absolute.push_back(next_world);
    }

    plan_      = planner_(*self_, sanitizer_.sanitize(*self_, absolute, visited_thresh_), visited_thresh_);
    final_dest = plan_.final_dest;
    plan_.override_non_locked = override_non_locked;

    // If no viable strides were produced, immediately request another plan so controllers
    // can try alternative inputs instead of getting stuck with a cleared request flag.
    if (plan_.strides.empty()) {
        path_requested = true;
        return;
    }

    if (runtime_) {
        runtime_->reset_plan_progress();
    }

    // Signal executor to re-evaluate plan
    input_event_ = true;
}

void AnimationUpdate::move(SDL_Point delta,
                           const std::string& animation,
                           bool               resort_z,
                           bool               override_non_locked) {
    if (!self_ || !self_->info) {
        return;
    }
    // Do not mutate Asset here; store request for executor
    pending_move_.delta        = delta;
    pending_move_.animation_id = animation;
    pending_move_.resort_z     = resort_z;
    pending_move_.override_non_locked = override_non_locked;
    move_pending_              = true;
    input_event_               = true;
}

void AnimationUpdate::clear_movement_plan() {
    plan_.strides.clear();
    plan_.sanitized_checkpoints.clear();
    plan_.final_dest = self_ ? self_->pos : SDL_Point{ 0, 0 };
    plan_.override_non_locked = true;
    final_dest       = plan_.final_dest;
    path_requested   = false;
    input_event_     = true;

    if (runtime_) {
        runtime_->reset_plan_progress();
    }
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

vibble::grid::Grid& AnimationUpdate::grid() const {
    if (grid_service_) {
        return *grid_service_;
    }
    return vibble::grid::global_grid();
}

int AnimationUpdate::effective_grid_resolution(std::optional<int> override_resolution) const {
    if (override_resolution.has_value()) {
        return vibble::grid::clamp_resolution(*override_resolution);
    }
    if (self_) {
        return vibble::grid::clamp_resolution(self_->grid_resolution);
    }
    return 0;
}

