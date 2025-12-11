#pragma once

#include <iosfwd>
#include <string>

namespace devmode {
namespace core {
class ManifestStore;
}

namespace manifest_utils {

struct RemoveAssetResult {
	bool removed = false;
	bool used_store = false;
};

RemoveAssetResult remove_asset_entry(core::ManifestStore* store, const std::string& asset_name, std::ostream* log = nullptr);

bool remove_manifest_asset_entry(const std::string& asset_name, std::ostream* log = nullptr);

}
}
