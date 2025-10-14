#include "animation_update_utils.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

#include "core/AssetsManager.hpp"
#include "map_generation/room.hpp"

namespace {

std::string normalize_room_type_string(const std::string& raw) {
    if (raw.empty()) {
        return std::string{};
    }

    std::string lowered = raw;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return lowered;
}

bool is_playable_room(const Room& room) {
    if (!room.room_area) {
        return false;
    }

    const std::string area_type = normalize_room_type_string(room.room_area->get_type());
    if (area_type == "room" || area_type == "trail") {
        return true;
    }

    const std::string room_type = normalize_room_type_string(room.type);
    return room_type == "room" || room_type == "trail";
}

}  // namespace

namespace animation_update::detail {

bool bottom_point_inside_playable_area(const Assets* assets, SDL_Point bottom_point) {
    if (!assets) {
        return false;
    }

    const std::vector<Room*>& rooms = assets->rooms();
    for (const Room* room : rooms) {
        if (!room || !room->room_area) {
            continue;
        }
        if (!is_playable_room(*room)) {
            continue;
        }
        if (room->room_area->contains_point(bottom_point)) {
            return true;
        }
    }

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

}  // namespace animation_update::detail

