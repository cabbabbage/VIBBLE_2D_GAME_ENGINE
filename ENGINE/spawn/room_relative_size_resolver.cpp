#include "room_relative_size_resolver.hpp"

#include <algorithm>
#include <cmath>

RoomRelativeSizeResolver::RoomRelativeSizeResolver(int original_width,
                                                   int original_height,
                                                   int current_width,
                                                   int current_height)
    : original_width_(sanitize_dimension(original_width, current_width)),
      original_height_(sanitize_dimension(original_height, current_height)),
      current_width_(sanitize_dimension(current_width, original_width_)),
      current_height_(sanitize_dimension(current_height, original_height_)),
      width_ratio_(safe_ratio(current_width_, original_width_)),
      height_ratio_(safe_ratio(current_height_, original_height_)),
      average_ratio_((width_ratio_ + height_ratio_) * 0.5) {}

int RoomRelativeSizeResolver::sanitize_dimension(int value, int fallback) {
    if (value > 0) return value;
    if (fallback > 0) return fallback;
    return 1;
}

double RoomRelativeSizeResolver::safe_ratio(int numerator, int denominator) {
    if (denominator <= 0) {
        return 1.0;
    }
    if (numerator <= 0) {
        return 0.0;
    }
    return static_cast<double>(numerator) / static_cast<double>(denominator);
}

int RoomRelativeSizeResolver::scale_count(int value) const {
    if (value <= 0) return 0;
    const double scaled = static_cast<double>(value) * average_ratio_;
    const int rounded = static_cast<int>(std::lround(scaled));
    return std::max(1, rounded);
}

std::pair<int, int> RoomRelativeSizeResolver::scale_count_range(int min_value, int max_value) const {
    if (max_value < min_value) {
        std::swap(min_value, max_value);
    }
    const int scaled_min = scale_count(min_value);
    const int scaled_max = std::max(scaled_min, scale_count(max_value));
    return {scaled_min, scaled_max};
}

int RoomRelativeSizeResolver::scale_length(int value) const {
    if (value <= 0) return 0;
    const double scaled = static_cast<double>(value) * average_ratio_;
    const int rounded = static_cast<int>(std::lround(scaled));
    return std::max(0, rounded);
}

