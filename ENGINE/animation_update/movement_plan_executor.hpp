#pragma once

#include <cstddef>

#include "stride_types.hpp"

class AnimationRuntime;

class MovementPlanExecutor {
public:
    bool tick(AnimationRuntime& up, Plan& plan, std::size_t& stride_index, int& stride_frame_counter);
};
