#pragma once

#include <cstdint>
#include <vector>

namespace lighting {

struct MeasureResult;

float computeAverageLuminance(const std::vector<std::uint8_t>& rgba,
                              int width,
                              int height);

float computeAverageLuminance(const MeasureResult& result);

} // namespace lighting

