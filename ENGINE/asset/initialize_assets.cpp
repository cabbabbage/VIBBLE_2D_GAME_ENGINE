#include "initialize_assets.hpp"
#include "AssetsManager.hpp"
#include "Asset.hpp"
#include "asset_info.hpp"
#include "asset_utils.hpp"
#include "active_assets_manager.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <memory>
void InitializeAssets::initialize(Assets& assets,
                                  std::vector<Asset>&& loaded,
                                  std::vector<Room*> rooms,
                                  int ,
                                  int ,
                                  int screen_center_x,
                                  int screen_center_y,
                                  int )
{
    std::cout << "[InitializeAssets] Initializing Assets manager...\n";
    assets.rooms_ = std::move(rooms);
    assets.all.reserve(loaded.size());
    while (!loaded.empty()) {
        Asset a = std::move(loaded.back());
        loaded.pop_back();
        if (!a.info) {
            std::cerr << "[InitializeAssets] Skipping asset: info is null\n";
            continue;
        }
        auto it = a.info->animations.find("default");
        if (it == a.info->animations.end() || it->second.frames.empty()) {
            std::cerr << "[InitializeAssets] Skipping asset '" << a.info->name
                      << "': missing or empty default animation\n";
            continue;
        }
        auto newAsset = std::make_unique<Asset>(std::move(a));
        Asset* raw = newAsset.get();
        set_view_recursive(raw, &assets.window);
        set_assets_owner_recursive(raw, &assets);
        assets.owned_assets.push_back(std::move(newAsset));
        assets.all.push_back(raw);
        raw->finalize_setup();
    }
    find_player(assets);
    assets.activeManager.initialize(assets.all, assets.player,
                                    screen_center_x, screen_center_y);
    assets.active_assets  = assets.activeManager.getActive();
    assets.closest_assets = assets.activeManager.getClosest();
    setup_shading_groups(assets);
    std::cout << "[InitializeAssets] Initialization base complete. Total assets: "
              << assets.all.size() << "\n";
    try {
        setup_static_sources(assets);
    } catch (const std::length_error& e) {
        std::cerr << "[InitializeAssets] light-gen failed: " << e.what() << "\n";
    }
    std::cout << "[InitializeAssets] All static sources set.\n";
    assets.activeManager.updateAssetVectors(assets.player,
                                            screen_center_x,
                                            screen_center_y);
    assets.window.zoom_scale(1.0, 200);
}

void InitializeAssets::find_player(Assets& assets) {
    for (Asset* asset : assets.all) {
        if (asset && asset->info && asset->info->type == "Player") {
            assets.player = asset;
            std::cout << "[InitializeAssets] Found player asset: "
                      << assets.player->info->name << "\n";
            break;
        }
    }
}

void InitializeAssets::set_shading_group_recursive(Asset& asset,
                                                   int group,
                                                   int ) {
    asset.set_shading_group(group);
    for (Asset* child : asset.children) {
        if (child) set_shading_group_recursive(*child, group, 0);
    }
}

void InitializeAssets::collect_assets_in_range(const Asset* asset,
                                               int cx,
                                               int cy,
                                               int r2,
                                               std::vector<Asset*>& result) {
    int dx = asset->pos_X - cx;
    int dy = asset->pos_Y - cy;
    if (dx * dx + dy * dy <= r2) {
        result.push_back(const_cast<Asset*>(asset));
    }
    for (Asset* child : asset->children) {
        if (child) collect_assets_in_range(child, cx, cy, r2, result);
    }
}

void InitializeAssets::setup_static_sources(Assets& assets) {
    std::function<void(Asset&)> recurse = [&](Asset& owner) {
        if (owner.info) {
            for (LightSource& light : owner.info->light_sources) {
                const int lx = owner.pos_X + light.offset_x;
                const int ly = owner.pos_Y + light.offset_y;
                const int r2 = light.radius * light.radius;
                std::vector<Asset*> targets;
                targets.reserve(assets.all.size());
                for (Asset* a : assets.all) {
                    if (!a || !a->info) continue;
                    collect_assets_in_range(a, lx, ly, r2, targets);
                }
                for (Asset* t : targets) {
                    if (t && t->info) {
                        t->add_static_light_source(&light, lx, ly, &owner);
                    }
                }
            }
        }
        for (Asset* child : owner.children) {
            if (child) recurse(*child);
        }
    };
    for (Asset* owner : assets.all)
        if (owner)
            recurse(*owner);
}

void InitializeAssets::setup_shading_groups(Assets& assets) {
    int group = 1;
    for (Asset* a : assets.all) {
        if (!a || !a->info) continue;
        set_shading_group_recursive(*a, group, assets.num_groups_);
        group = (group == assets.num_groups_) ? 1 : group + 1;
    }
}
