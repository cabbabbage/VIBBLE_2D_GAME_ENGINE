#include "dev_mode/animation_regenerator.hpp"

#include <SDL.h>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_set>

#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"
#include "core/AssetsManager.hpp"
#include "core/manifest/manifest_loader.hpp"

namespace fs = std::filesystem;

namespace {

bool remove_path_if_exists(const fs::path& target, bool recursive) {
    std::error_code ec;
    if (!fs::exists(target, ec)) {
        return false;
    }

    if (recursive) {
        fs::remove_all(target, ec);
    } else {
        fs::remove(target, ec);
    }

    if (ec) {
        std::cerr << "[AnimationRegenerator] Failed to remove '" << target.string()
                  << "': " << ec.message() << "\n";
        return false;
    }

    return true;
}

bool clear_animation_cache(const fs::path& cache_root,
                           const std::string& asset_name,
                           const std::string& animation_id) {
    bool cleared_any = false;
    const fs::path anim_dir = cache_root / asset_name / "animations" / animation_id;
    const fs::path meta_file =
        cache_root / ".asset_cache" / "animations" / asset_name / (animation_id + ".json");

    cleared_any |= remove_path_if_exists(anim_dir, /*recursive=*/true);
    cleared_any |= remove_path_if_exists(meta_file, /*recursive=*/false);

    return cleared_any;
}

bool run_python_regen(const fs::path& project_root,
                      const std::string& asset_name,
                      const std::string& animation_id) {
    const fs::path python_script = project_root / "tools" / "asset_tool.py";
    const fs::path manifest_path = project_root / "manifest.json";
    const fs::path cache_root = project_root / "cache";

    std::ostringstream oss;
    oss << "python \"" << python_script.string() << "\" "
        << "\"" << manifest_path.string() << "\" "
        << "\"" << cache_root.string() << "\" "
        << "\"" << asset_name << "\" "
        << "\"" << animation_id << "\"";
    const std::string cmd = oss.str();

    std::cout << "[AnimationRegenerator] Running: " << cmd << "\n";
    const int ret = std::system(cmd.c_str());
    if (ret != 0) {
        std::cerr << "[AnimationRegenerator] asset_tool.py failed for " << asset_name
                  << "::" << animation_id << " (exit code " << ret << ")\n";
        return false;
    }

    return true;
}

void refresh_loaded_instances(Assets* assets, const std::shared_ptr<AssetInfo>& info) {
    if (!assets || !info) {
        return;
    }

    std::unordered_set<Asset*> visited;
    auto refresh = [&](Asset* asset) {
        if (!asset || asset->info.get() != info.get()) {
            return;
        }
        if (!visited.insert(asset).second) {
            return;
        }

        asset->clear_render_caches();
        asset->clear_downscale_cache();
        asset->set_final_texture(nullptr);
        asset->current_frame = nullptr;
        asset->frame_progress = 0.0f;
        asset->static_frame = false;

        std::string desired = asset->current_animation.empty()
                                   ? std::string{"default"}
                                   : asset->current_animation;
        auto it = info->animations.find(desired);
        if (it == info->animations.end()) {
            it = info->animations.find("default");
        }
        if (it == info->animations.end() && !info->animations.empty()) {
            it = info->animations.begin();
        }

        if (it != info->animations.end()) {
            auto& anim = it->second;
            asset->current_animation = it->first;
            asset->current_frame = anim.get_first_frame();
            asset->static_frame = anim.is_frozen() || anim.locked;
        } else {
            asset->current_animation.clear();
            asset->current_frame = nullptr;
        }

        asset->refresh_cached_dimensions();
    };

    for (Asset* asset : assets->all) {
        refresh(asset);
    }
    for (const auto& owned : assets->owned_assets) {
        refresh(owned.get());
    }

    assets->mark_active_assets_dirty();
}

} // namespace

namespace devmode {

AnimationRegenerationResult AnimationRegenerator::regenerate_animation(
    Assets* assets,
    const std::shared_ptr<AssetInfo>& info,
    const std::string& animation_id) {
    AnimationRegenerationResult result{};

    if (!assets || !info) {
        std::cerr << "[AnimationRegenerator] Missing assets or asset info; skipping regeneration\n";
        return result;
    }
    if (animation_id.empty()) {
        std::cerr << "[AnimationRegenerator] No animation id provided for regeneration of "
                  << info->name << "\n";
        return result;
    }

    const std::string asset_name = info->name;
    if (asset_name.empty()) {
        std::cerr << "[AnimationRegenerator] Asset name missing for animation regeneration\n";
        return result;
    }

    const fs::path project_root = fs::path(manifest::manifest_path()).parent_path();
    const fs::path cache_root = project_root / "cache";

    result.cache_cleared = clear_animation_cache(cache_root, asset_name, animation_id);

    result.python_launched = true;
    result.python_success =
        run_python_regen(project_root, asset_name, animation_id);
    if (!result.python_success) {
        return result;
    }

    if (!info->reload_animations_from_disk()) {
        std::cerr << "[AnimationRegenerator] Failed to reload animations for " << asset_name << "\n";
        return result;
    }
    result.reloaded = true;

    SDL_Renderer* renderer = assets->renderer();
    if (!renderer) {
        std::cerr << "[AnimationRegenerator] No renderer available; cannot reload textures for "
                  << asset_name << "\n";
        return result;
    }

    info->loadAnimations(renderer);
    refresh_loaded_instances(assets, info);
    result.refreshed_instances = true;

    return result;
}

} // namespace devmode
