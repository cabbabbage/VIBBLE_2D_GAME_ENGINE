#pragma once

#include <cstddef>

#include "stride_types.hpp"

class AnimationRuntime;

// Executes a movement Plan by advancing frames and applying motion.
class MovementPlanExecutor {
public:
    bool tick(AnimationRuntime& up, Plan& plan, std::size_t& stride_index, int& stride_frame_counter);
};
