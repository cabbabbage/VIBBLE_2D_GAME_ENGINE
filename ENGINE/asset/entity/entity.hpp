#ifndef TYPE_H
#define TYPE_H
#include <vector>
#include <string>
#include "damage.hpp"

class Entity {
    public:
        enum Type{
            PLAYER,
            ENEMY,
            RANGEDENEMY
};

    virtual int getHealth();
    virtual Type getType();
    virtual bool isDamageable();
    virtual int dealDamage(Damage *damage);

    Damage damage;

    protected:
        int health;
        int speed;
        Type type;
        bool can_damage = true;
        std::vector<std::string> items;
};
#endif