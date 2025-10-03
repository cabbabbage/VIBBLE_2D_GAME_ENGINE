#include "rebuild_assets.hpp"
#include "asset/asset_library.hpp"
#include "map_generation/generate_rooms.hpp"
#include <filesystem>
#include <iostream>
#include <cstdlib>
namespace fs = std::filesystem;

RebuildAssets::RebuildAssets(SDL_Renderer* renderer, const std::string& map_dir) {
	try {
		std::cout << "[RebuildAssets] Removing old cache directory...\n";
		fs::remove_all("cache");
		std::cout << "[RebuildAssets] Cache directory deleted.\n";
		std::cout << "[RebuildAssets] Creating new AssetLibrary...\n";
		AssetLibrary asset_lib;
		asset_lib.load_all_from_SRC();
		asset_lib.loadAllAnimations(renderer);
		std::cout << "[RebuildAssets] AssetLibrary rebuilt successfully.\n";
		std::cout << "[RebuildAssets] Running cartoon effect Python script...\n";
	} catch (const std::exception& e) {
		std::cerr << "[RebuildAssets] Error: " << e.what() << "\n";
	}
}
