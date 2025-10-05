#pragma once

#include <ostream>
#include <string>

#include <nlohmann/json_fwd.hpp>

namespace devmode {

bool write_map_info_json(const std::string& path,
                         const nlohmann::json& data,
                         std::ostream& log);

}
