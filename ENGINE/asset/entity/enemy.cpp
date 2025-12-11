#include "enemy.hpp"
#include "entity.hpp"

int Enemy::getHealth() {
    return health;
};

Entity::Type Enemy::getType() {
    return type;
};

bool Enemy::isDamageable() {
    return can_damage;
};

int Enemy::dealDamage(Damage *damage) {
    if(damage->damage_active == true) {
        for(int i = 0; i < damage->damage_time_in_frames; i++) {
            health -= damage->damage_amt_per_frame;
        }
    }

    return health;
}