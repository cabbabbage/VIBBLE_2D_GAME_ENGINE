#ifndef PLAYER_H
#define PLAYER_H
#include <vector>
#include <string>
#include "entity.hpp"

class Player : public Entity {
public:
    Player() {
        type = PLAYER;
        health = 150;
        speed = 1;
    }

    Type getType() override;
    int getHealth() override;
    bool isDamageable() override;

    int dealDamage(Damage *damage) override;

};

#endif