#include "initialize_assets.hpp"
#include "AssetsManager.hpp"
#include "Asset.hpp"
#include "asset_info.hpp"
#include "asset_types.hpp"
#include "asset_utils.hpp"
#include "utils/log.hpp"
#include <algorithm>
#include <iostream>
#include <memory>
#include <SDL.h>

void InitializeAssets::initialize(Assets& assets,
                                  std::vector<Room*> rooms,
                                  int,
                                  int,
                                  int /*screen_center_x*/,
                                  int /*screen_center_y*/,
                                  int)
{
        vibble::log::debug("[InitializeAssets] Initializing Assets manager...");
        assets.set_rooms(std::move(rooms));
        assets.all.clear();
        auto grid_assets = assets.world_grid().all_assets();
        assets.all.reserve(grid_assets.size());
        for (Asset* raw : grid_assets) {
                if (!raw) {
                        continue;
                }
                if (!raw->info) {
                        vibble::log::debug("[InitializeAssets] Skipping asset: info is null");
                        assets.world_grid().remove_asset(raw);
                        continue;
                }
                auto it = raw->info->animations.find("default");
                if (it == raw->info->animations.end() || it->second.frames.empty()) {
                        vibble::log::debug("[InitializeAssets] Skipping asset '" + raw->info->name + "': missing or empty default animation");
                        assets.world_grid().remove_asset(raw);
                        continue;
                }
                set_camera_recursive(raw, &assets.getView());
                set_assets_owner_recursive(raw, &assets);
                assets.all.push_back(raw);
                // Assets should already be finalized by AssetLoader::finalizeAssets().
                // Guard to avoid double-initialization; finalize only if somehow not finalized.
                if (!raw->is_finalized()) {
                    vibble::log::debug("[InitializeAssets] Asset '" + (raw->info ? raw->info->name : std::string{"<null>"}) + "' not finalized by loader; finalizing now.");
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
        vibble::log::debug("[InitializeAssets] Initialization base complete. Total assets: " + std::to_string(assets.all.size()));
    // Leave rebuild to the runtime update once chunks are available.

}

void InitializeAssets::find_player(Assets& assets) {
        for (Asset* asset : assets.all) {
                if (asset && asset->info && asset->info->type == asset_types::player) {
			assets.player = asset;
                        vibble::log::debug("[InitializeAssets] Found player asset: " + assets.player->info->name);
                        break;
                }
	}
}
