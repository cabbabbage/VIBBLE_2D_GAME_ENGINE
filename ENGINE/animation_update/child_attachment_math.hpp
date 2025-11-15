#pragma once

inline float mirrored_child_rotation(bool parent_flipped, float child_degrees) {
    return parent_flipped ? -child_degrees : child_degrees;
}
