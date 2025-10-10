#pragma once

#include <string>
#include <vector>

namespace devmode {
namespace area_mode {

inline const std::vector<std::string>& area_types() {
    static const std::vector<std::string> kTypes = {
        "all",
        "impassable",
        "trigger",
        "child",
        "spawning"
};
    return kTypes;
}

}
}
