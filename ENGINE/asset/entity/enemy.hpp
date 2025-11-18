#ifndef ENEMY_H
#define ENEMY_H
#include <vector>
#include <string>
#include "entity.hpp"

class Enemy : public Entity {
    Type type = ENEMY;
    int health = 150;
    int speed = 1;
    bool can_damage = true;
    std::vector<std::string> items;

public:
    Type getType() override;
    int getHealth() override;
    bool isDamageable() override;
    int dealDamage(Damage *damage) override;
};
#endif