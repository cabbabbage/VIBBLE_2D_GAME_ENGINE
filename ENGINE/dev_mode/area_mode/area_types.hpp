#pragma once

#include <string>
#include <vector>

namespace devmode {
namespace area_mode {

// Canonical list of room/area types for Area Mode filtering and creation.
// Order determines header checkbox order.
inline const std::vector<std::string>& area_types() {
    static const std::vector<std::string> kTypes = {
        "impasable", // note: spelled per request
        "spacing",
        "trigger",
        "child",
        "spawning"
    };
    return kTypes;
}

} // namespace area_mode
} // namespace devmode

