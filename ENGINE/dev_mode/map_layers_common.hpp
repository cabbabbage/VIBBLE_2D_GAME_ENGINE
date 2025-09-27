#pragma once

#include <algorithm>

namespace map_layers {

constexpr int kCandidateRangeMax = 128;

inline int clamp_candidate_min(int value) {
    return std::clamp(value, 0, kCandidateRangeMax);
}

inline int clamp_candidate_max(int min_value, int max_value) {
    const int clamped_min = clamp_candidate_min(min_value);
    return std::clamp(max_value, clamped_min, kCandidateRangeMax);
}

}  // namespace map_layers

