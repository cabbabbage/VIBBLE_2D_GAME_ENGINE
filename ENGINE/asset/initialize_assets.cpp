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
                                  std::vector<Room*> rooms,
                                  int,
                                  int,
                                  int /*screen_center_x*/,
                                  int /*screen_center_y*/,
                                  int)
{
        if (kAssetLoggingEnabled) {
                std::cout << "[InitializeAssets] Initializing Assets manager...\n";
        }
        assets.set_rooms(std::move(rooms));
        assets.all.clear();
        auto grid_assets = assets.world_grid().all_assets();
        assets.all.reserve(grid_assets.size());
        for (Asset* raw : grid_assets) {
                if (!raw) {
                        continue;
                }
                if (!raw->info) {
                        if (kAssetLoggingEnabled) {
                                std::cerr << "[InitializeAssets] Skipping asset: info is null\n";
                        }
                        assets.world_grid().remove_asset(raw);
                        continue;
                }
                auto it = raw->info->animations.find("default");
                if (it == raw->info->animations.end() || it->second.frames.empty()) {
                        if (kAssetLoggingEnabled) {
                                std::cerr << "[InitializeAssets] Skipping asset '" << raw->info->name
                                << "': missing or empty default animation\n";
                        }
                        assets.world_grid().remove_asset(raw);
                        continue;
                }
                set_camera_recursive(raw, &assets.getView());
                set_assets_owner_recursive(raw, &assets);
                assets.all.push_back(raw);
                // Assets should already be finalized by AssetLoader::finalizeAssets().
                // Guard to avoid double-initialization; finalize only if somehow not finalized.
                if (!raw->is_finalized()) {
                    if (kAssetLoggingEnabled) {
                        std::cerr << "[InitializeAssets] Asset '" << (raw->info ? raw->info->name : std::string{"<null>"})
                                  << "' not finalized by loader; finalizing now.\n";
                    }
                    raw->finalize_setup();
                }
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
