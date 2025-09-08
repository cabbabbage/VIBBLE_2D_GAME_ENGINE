#include "child_loader.hpp"
#include "asset/asset_info.hpp"
#include <filesystem>
#include <iostream>
using nlohmann::json;
namespace fs = std::filesystem;

void ChildLoader::load_children(AssetInfo& info,
                                const json& data,
                                const std::string& dir_path) {
	info.children.clear();
	if (!data.contains("child_assets") || !data["child_assets"].is_array())
	return;
	for (const auto& entry : data["child_assets"]) {
		if (!entry.is_object()) continue;
		ChildInfo ci;
		ci.json_path = entry.value("json_path", std::string{});
		if (!ci.json_path.empty()) {
			fs::path full = fs::path(dir_path) / ci.json_path;
			ci.json_path = full.string();
		}
		ci.area_name = entry.value("area_name", std::string{});
		ci.z_offset  = entry.value("z_offset", 0);
		info.children.emplace_back(std::move(ci));
	}
}
