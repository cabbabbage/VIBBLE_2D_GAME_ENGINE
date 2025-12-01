#pragma once

#include <cstdint>

#include "utils/light_source.hpp"

class LightFlickerCalculator {
public:
    static float compute_multiplier(const LightSource& light, float time_seconds);
};
