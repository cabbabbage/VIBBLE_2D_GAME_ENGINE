#include "find_current_room.hpp"
#include "map_generation/room.hpp"
#include "asset/Asset.hpp"
#include "utils/area.hpp"
#include "utils/range_util.hpp"

#include <limits>
#include <cmath>

CurrentRoomFinder::CurrentRoomFinder(std::vector<Room*>& rooms, Asset*& player)
: rooms_(rooms), player_(player) {}

void CurrentRoomFinder::setRooms(std::vector<Room*>& rooms) {
    rooms_ = rooms;
}

void CurrentRoomFinder::setPlayer(Asset*& player) {
    player_ = player;
}

Room* CurrentRoomFinder::getCurrentRoom() const {
    if (!player_) return nullptr;

    const int px = player_->pos.x;
    const int py = player_->pos.y;
    Room* best = nullptr;

    for (Room* r : rooms_) {
        if (!r || !r->room_area) continue;
        if (r->room_area->contains_point(SDL_Point{px, py})) {
            best = r;
            break;
        }
    }
    if (best) return best;

    double best_dist = std::numeric_limits<double>::max();
    SDL_Point player_pos{px, py};

    for (Room* r : rooms_) {
        if (!r || !r->room_area) continue;
        SDL_Point center = r->room_area->get_center();
        double d = Range::get_distance(player_pos, center);
        if (d < best_dist) {
            best_dist = d;
            best = r;
        }
    }
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
