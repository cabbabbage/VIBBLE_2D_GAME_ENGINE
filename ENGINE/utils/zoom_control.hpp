
#pragma once

#include <vector>
#include <string>

class view;
class Room;
class Asset;

class ZoomControl {
public:
    ZoomControl(view& window, std::vector<Room*>& rooms, Asset*& player);

    void set_up_rooms();
    void update(Room* cur);

private:
    double compute_room_scale_from_area(const Room* room) const;

    view&                 window_;
    std::vector<Room*>&   rooms_;
    Asset*&               player_;

    class CurrentRoomFinder* finder_; // forward-owned pointer

    Room*   current_room_;
    Room*   starting_room_;
    double  starting_area_;
};
