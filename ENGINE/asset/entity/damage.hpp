#ifndef DAMAGE_H
#define DAMAGE_H
#include "asset/Asset.hpp"

struct Damage {
    int damage_amt_per_frame = 1;
    bool damage_active = true;
    int damage_time_in_frames = 1;

    Asset* self = nullptr;
};
#endif
