#include "find_current_room.hpp"
#include "room/room.hpp"
#include "asset/Asset.hpp"
#include "utils/area.hpp"
#include <limits>
#include <cmath>
CurrentRoomFinder::CurrentRoomFinder(std::vector<Room*>& rooms, Asset*& player)
    : rooms_(rooms), player_(player) {}

void CurrentRoomFinder::setRooms(std::vector<Room*>& rooms) { rooms_ = rooms; }
void CurrentRoomFinder::setPlayer(Asset*& player) { player_ = player; }

Room* CurrentRoomFinder::getCurrentRoom() const {
    if (!player_) return nullptr;
    const int px = player_->pos_X;
    const int py = player_->pos_Y;
    Room* best = nullptr;
    for (Room* r : rooms_) {
        if (!r || !r->room_area) continue;
        if (r->room_area->contains_point({px, py})) {
            best = r;
            break;
        }
    }
    if (best) return best;
    double best_d2 = std::numeric_limits<double>::max();
    for (Room* r : rooms_) {
        if (!r || !r->room_area) continue;
        auto c = r->room_area->get_center();
        double dx = double(px - c.first);
        double dy = double(py - c.second);
        double d2 = dx*dx + dy*dy;
        if (d2 < best_d2) {
            best_d2 = d2;
            best = r;
        }
    }
    return best;
}

Room* CurrentRoomFinder::getNeighboringRoom(Room* current) const {
    if (!current) return nullptr;
    if (!current->connected_rooms.empty())
        return current->connected_rooms.front();
    if (current->left_sibling)  return current->left_sibling;
    if (current->right_sibling) return current->right_sibling;
    return nullptr;
}
