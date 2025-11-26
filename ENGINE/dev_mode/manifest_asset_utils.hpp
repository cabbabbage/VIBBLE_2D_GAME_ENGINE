#pragma once

#include <iosfwd>
#include <string>

namespace devmode {
namespace manifest_utils {

bool remove_manifest_asset_entry(const std::string& asset_name, std::ostream* log = nullptr);

}
} // namespace devmode::manifest_utils
