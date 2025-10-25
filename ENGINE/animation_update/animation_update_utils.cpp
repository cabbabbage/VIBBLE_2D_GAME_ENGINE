#include "animation_update_utils.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

#include "core/AssetsManager.hpp"
#include "map_generation/room.hpp"

namespace {

struct PlayableRoomsCacheEntry {
    const Room* last_containing_room = nullptr;
    std::unordered_map<const Room*, bool> playable_lookup;
    std::uintptr_t rooms_identity = 0;
    std::size_t rooms_size = 0;
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

bool bottom_point_inside_playable_area(const Assets* assets, SDL_Point bottom_point) {
    if (!assets) {
        return false;
    }

    auto& cache_entry = playable_rooms_cache()[assets];

    const std::vector<Room*>& rooms = assets->rooms();
    const std::uintptr_t identity = rooms.empty() ? 0 : reinterpret_cast<std::uintptr_t>(rooms.data());
    if (cache_entry.rooms_identity != identity || cache_entry.rooms_size != rooms.size()) {
        cache_entry.rooms_identity = identity;
        cache_entry.rooms_size     = rooms.size();
        cache_entry.last_containing_room = nullptr;
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

