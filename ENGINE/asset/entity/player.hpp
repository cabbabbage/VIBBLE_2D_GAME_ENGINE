#include <vector>
#include <string>
#include "entity.hpp"

class player : public entity {
    int health = 150;
    int speed = 1;
    bool can_damage = true;
    std::vector<std::string> items;
};