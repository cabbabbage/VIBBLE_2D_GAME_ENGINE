#pragma once

#include <vector>
#include <utility>

class Room;
class Asset;

class CurrentRoomFinder {
public:
    CurrentRoomFinder(std::vector<Room*>& rooms, Asset*& player);
    Room* getCurrentRoom() const;
    Room* getNeighboringRoom(Room* current) const;
    void setRooms(std::vector<Room*>& rooms);
    void setPlayer(Asset*& player);
private:
    std::vector<Room*>& rooms_;
    Asset*&             player_;
};
