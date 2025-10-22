#include "utils/Luminance.hpp"

#include "utils/RenderReadback.hpp"

#include <algorithm>
#include <cstddef>

namespace lighting {

namespace {
constexpr double kRedWeight   = 0.2126;
constexpr double kGreenWeight = 0.7152;
constexpr double kBlueWeight  = 0.0722;
}

float computeAverageLuminance(const std::vector<std::uint8_t>& rgba,
                              int width,
                              int height) {
    if (width <= 0 || height <= 0 || rgba.empty()) {
        return 0.0f;
    }

    const std::size_t pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (rgba.size() < pixel_count * 4) {
        return 0.0f;
    }

    const double denom = static_cast<double>(pixel_count);
    double       accum = 0.0;
    for (std::size_t index = 0; index < pixel_count; ++index) {
        const std::size_t base = index * 4;
        const double      r    = static_cast<double>(rgba[base + 0]) / 255.0;
        const double      g    = static_cast<double>(rgba[base + 1]) / 255.0;
        const double      b    = static_cast<double>(rgba[base + 2]) / 255.0;
        const double      a    = static_cast<double>(rgba[base + 3]) / 255.0;

        const double luminance = (r * kRedWeight) + (g * kGreenWeight) + (b * kBlueWeight);
        accum += luminance * a;
    }

    return static_cast<float>(accum / denom);
}

float computeAverageLuminance(const MeasureResult& result) {
    if (!result.success()) {
        return 0.0f;
    }
    return computeAverageLuminance(result.pixels, result.width, result.height);
}

} // namespace lighting

