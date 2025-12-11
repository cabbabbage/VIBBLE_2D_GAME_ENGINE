#include "entity.hpp"
#include "player.hpp"

using namespace std;

int Player::getHealth() {
    return health;
};

Entity::Type Player::getType() {
    return type;
};

bool Player::isDamageable() {
    return can_damage;
};

int Player::dealDamage(Damage *damage) {
    if(damage->damage_active == true) {
        for(int i = 0; i < damage->damage_time_in_frames; i++) {
            health -= damage->damage_amt_per_frame;
        }
    }

    return health;
}