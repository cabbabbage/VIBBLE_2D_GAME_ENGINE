#include "find_current_room.hpp"
#include "map_generation/room.hpp"
#include "asset/Asset.hpp"
#include "utils/area.hpp"
#include "utils/range_util.hpp"

#include <limits>
#include <cmath>

CurrentRoomFinder::CurrentRoomFinder(std::vector<Room*>& rooms, Asset*& player) : rooms_(&rooms), player_(&player), last_room_(nullptr) {}

void CurrentRoomFinder::setRooms(std::vector<Room*>& rooms) {
    rooms_ = &rooms;
    last_room_ = nullptr;
}

void CurrentRoomFinder::setPlayer(Asset*& player) {
    player_ = &player;
    last_room_ = nullptr;
}

Room* CurrentRoomFinder::getCurrentRoom() const {
    auto* rooms = rooms_;
    Asset* player = player_ ? *player_ : nullptr;

    if (!player) {
        last_room_ = nullptr;
        return nullptr;
    }

    const int px = player->pos.x;
    const int py = player->pos.y;
    auto contains_player = [&](Room* room) -> bool {
        return room &&
               room->room_area &&
               room->room_area->contains_point(SDL_Point{px, py});
    };

    auto try_room = [&](Room* room) -> Room* {
        if (contains_player(room)) {
            last_room_ = room;
            return room;
        }
        return nullptr;
    };

    if (Room* matched = try_room(last_room_)) return matched;

    if (last_room_) {
        for (Room* connected : last_room_->connected_rooms) {
            if (Room* matched = try_room(connected)) return matched;
        }
        if (Room* matched = try_room(last_room_->left_sibling)) return matched;
        if (Room* matched = try_room(last_room_->right_sibling)) return matched;
    }

    Room* best = nullptr;

    if (!rooms) {
        last_room_ = nullptr;
        return nullptr;
    }

    for (Room* r : *rooms) {
        if (!r || !r->room_area) continue;
        if (r->room_area->contains_point(SDL_Point{px, py})) {
            best = r;
            break;
        }
    }
    if (best) {
        last_room_ = best;
        return best;
    }

    double best_dist = std::numeric_limits<double>::max();
    SDL_Point player_pos{px, py};

    for (Room* r : *rooms) {
        if (!r || !r->room_area) continue;
        SDL_Point center = r->room_area->get_center();
        double d = Range::get_distance(player_pos, center);
        if (d < best_dist) {
            best_dist = d;
            best = r;
        }
    }
    last_room_ = best;
    return best;
}

Room* CurrentRoomFinder::getNeighboringRoom(Room* current) const {
    if (!current) return nullptr;

    if (!current->connected_rooms.empty())
        return current->connected_rooms.front();
    if (current->left_sibling)
        return current->left_sibling;
    if (current->right_sibling)
        return current->right_sibling;

    return nullptr;
}
