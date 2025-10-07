#pragma once

#include <cstddef>

#include "stride_types.hpp"

class AnimationUpdate;

class StridePlayer {
public:
    bool tick(AnimationUpdate& up, Plan& plan,
              std::size_t& stride_index, int& stride_frame_counter);
};