#include "asset_library.hpp"
#include <filesystem>
#include <iostream>
#include <iomanip>
namespace fs = std::filesystem;

AssetLibrary::AssetLibrary() {
	load_all_from_SRC();
}

void AssetLibrary::load_all_from_SRC() {
	const std::string base_path = "SRC/";
	if (!fs::exists(base_path) || !fs::is_directory(base_path)) {
		std::cerr << "[AssetLibrary] Invalid path: " << base_path << "\n";
		return;
	}
	int loaded = 0;
	int failed = 0;
	for (const auto& entry : fs::directory_iterator(base_path)) {
		if (!entry.is_directory()) continue;
		std::string name = entry.path().filename().string();
		try {
			auto info = std::make_shared<AssetInfo>(name);
			info_by_name_[name] = info;
			++loaded;
		} catch (const std::exception&) {
			++failed;
		}
		std::cout << "[AssetLibrary] Loaded: " << loaded
		<< "   Failed: " << failed
		<< "   Current: " << std::left << std::setw(20) << name << "\r" << std::flush;
	}
	std::cout << std::endl
	<< "[AssetLibrary] Loaded " << info_by_name_.size() << " assets.\n";
}

std::shared_ptr<AssetInfo> AssetLibrary::get(const std::string& name) const {
	auto it = info_by_name_.find(name);
	if (it != info_by_name_.end()) {
		return it->second;
	}
	return nullptr;
}

const std::unordered_map<std::string, std::shared_ptr<AssetInfo>>&
AssetLibrary::all() const {
	return info_by_name_;
}

void AssetLibrary::loadAllAnimations(SDL_Renderer* renderer) {
	for (auto& [name, info] : info_by_name_) {
		info->loadAnimations(renderer);
	}
}
