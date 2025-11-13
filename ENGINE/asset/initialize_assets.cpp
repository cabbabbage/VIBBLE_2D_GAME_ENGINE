#include "initialize_assets.hpp"
#include "AssetsManager.hpp"
#include "Asset.hpp"
#include "asset_info.hpp"
#include "asset_types.hpp"
#include "asset_utils.hpp"
#include <algorithm>
#include <iostream>
#include <memory>
#include <SDL.h>

namespace {
#ifdef VIBBLE_DEBUG_ASSET_LOGS
constexpr bool kAssetLoggingEnabled = true;
#else
constexpr bool kAssetLoggingEnabled = false;
#endif
}

void InitializeAssets::initialize(Assets& assets,
                                  std::vector<std::unique_ptr<Asset>>&& loaded,
                                  std::vector<Room*> rooms,
                                  int ,
                                  int ,
                                  int screen_center_x,
                                  int screen_center_y,
                                  int )
{
        if (kAssetLoggingEnabled) {
                std::cout << "[InitializeAssets] Initializing Assets manager...\n";
        }
        assets.set_rooms(std::move(rooms));
        assets.all.reserve(loaded.size());
        while (!loaded.empty()) {
                std::unique_ptr<Asset> asset = std::move(loaded.back());
                loaded.pop_back();
                if (!asset) {
                        continue;
                }
                if (!asset->info) {
                        if (kAssetLoggingEnabled) {
                                std::cerr << "[InitializeAssets] Skipping asset: info is null\n";
                        }
                        continue;
                }
                auto it = asset->info->animations.find("default");
                if (it == asset->info->animations.end() || it->second.frames.empty()) {
                        if (kAssetLoggingEnabled) {
                                std::cerr << "[InitializeAssets] Skipping asset '" << asset->info->name
                                << "': missing or empty default animation\n";
                        }
                        continue;
                }
                Asset* raw = asset.get();
                set_camera_recursive(raw, &assets.getView());
                set_assets_owner_recursive(raw, &assets);
                assets.owned_assets.push_back(std::move(asset));
                assets.all.push_back(raw);
                raw->finalize_setup();
                // Initialize tiling for tileable assets on load
                try {
                    if (raw->info && raw->info->tillable) {
                        auto t = assets.compute_tiling_for_asset(raw);
                        if (t && t->is_valid()) {
                            raw->set_tiling_info(*t);
                        } else {
                            raw->set_tiling_info(std::nullopt);
                        }
                    } else {
                        raw->set_tiling_info(std::nullopt);
                    }
                } catch (...) {
                    // Leave tiling unset on any failure
                    raw->set_tiling_info(std::nullopt);
                }
        }
	find_player(assets);
        // Do not trigger active asset rebuild during construction.
        // Just mark lists dirty; the first rebuild will occur lazily on
        // the first update after the world grid populates active chunks.
        assets.mark_active_assets_dirty();
        if (kAssetLoggingEnabled) {
                std::cout << "[InitializeAssets] Initialization base complete. Total assets: "
                << assets.all.size() << "\n";
        }
    // Leave rebuild to the runtime update once chunks are available.

}

void InitializeAssets::find_player(Assets& assets) {
        for (Asset* asset : assets.all) {
                if (asset && asset->info && asset->info->type == asset_types::player) {
			assets.player = asset;
                        if (kAssetLoggingEnabled) {
                                std::cout << "[InitializeAssets] Found player asset: "
                                << assets.player->info->name << "\n";
                        }
                        break;
                }
	}
}
