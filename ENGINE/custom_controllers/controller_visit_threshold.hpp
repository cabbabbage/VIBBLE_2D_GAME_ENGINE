#pragma once

#include <algorithm>

#include "custom_controllers/controller_path_utils.hpp"

class Asset;

namespace controller_utils {

inline int controller_visit_threshold(const Asset* asset) {
    return std::max(2, controller_paths::default_visit_threshold(asset));
}

}

