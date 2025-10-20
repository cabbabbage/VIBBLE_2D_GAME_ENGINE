#include "asset_library.hpp"

#include "core/manifest/manifest_loader.hpp"

#include <iomanip>
#include <iostream>
#include <chrono>
#include "utils/log.hpp"

AssetLibrary::AssetLibrary(bool auto_load) {
        if (auto_load) {
                load_all_from_SRC();
        }
}

void AssetLibrary::load_all_from_SRC() {
        info_by_name_.clear();
        animations_fully_cached_ = false;

        manifest::ManifestData manifest;
        try {
                manifest = manifest::load_manifest();
        } catch (const std::exception& error) {
                log::error(std::string("[AssetLibrary] Failed to load manifest: ") + error.what());
                return;
        }

        if (!manifest.assets.is_object()) {
                log::error("[AssetLibrary] Manifest assets section is missing or malformed.");
                return;
        }

        int loaded = 0;
        int failed = 0;
        const auto start_ms = std::chrono::steady_clock::now();

        for (auto it = manifest.assets.begin(); it != manifest.assets.end(); ++it) {
                const std::string name = it.key();
                const auto& metadata = it.value();

                if (!metadata.is_object()) {
                        ++failed;
                        log::warn(std::string("[AssetLibrary] Manifest entry for asset '") + name + "' is not a JSON object.");
                        continue;
                }

                try {
                        std::shared_ptr<AssetInfo> info;
                        const bool has_metadata = metadata.is_object() && !metadata.empty();
                        if (has_metadata) {
                                info = AssetInfo::from_manifest_entry(name, metadata);
                        } else {
#if ASSET_INFO_ENABLE_INFO_JSON_COMPAT
                                info = std::make_shared<AssetInfo>(name);
#else
                                info = AssetInfo::from_manifest_entry(name, nlohmann::json::object());
#endif
                        }
                        info_by_name_[name] = info;
                        ++loaded;
                } catch (const std::exception& error) {
                        ++failed;
                        log::warn(std::string("[AssetLibrary] Failed to load asset '") + name + "': " + error.what());
                } catch (...) {
                        ++failed;
                        log::warn(std::string("[AssetLibrary] Failed to load asset '") + name + "' due to an unknown error.");
                }
        }
        const auto end_ms = std::chrono::steady_clock::now();
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_ms - start_ms).count();
        log::info(std::string("[AssetLibrary] Loaded ") + std::to_string(info_by_name_.size()) +
                  " assets (ok=" + std::to_string(loaded) + ", failed=" + std::to_string(failed) + ") in " +
                  std::to_string(elapsed_ms) + "ms");
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
    if (!renderer) {
        return;
    }

    const auto begin = std::chrono::steady_clock::now();
    std::size_t loaded = 0;
    for (auto& [name, info] : info_by_name_) {
        if (!info) {
            continue;
        }
        info->loadAnimations(renderer);
        ++loaded;
    }
    const auto end = std::chrono::steady_clock::now();
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();
    log::info(std::string("[AssetLibrary] Preloaded animations for ") + std::to_string(loaded) +
              " asset(s) in " + std::to_string(elapsed_ms) + "ms");
    animations_fully_cached_ = true;
}

void AssetLibrary::ensureAllAnimationsLoaded(SDL_Renderer* renderer) {
    if (!renderer || animations_fully_cached_) {
        return;
    }

    const auto begin = std::chrono::steady_clock::now();
    std::size_t loaded_now = 0;
    std::size_t already_cached = 0;
    for (auto& [name, info] : info_by_name_) {
        if (!info) {
            continue;
        }
        if (!info->animations.empty()) {
            ++already_cached;
            continue;
        }
        info->loadAnimations(renderer);
        ++loaded_now;
    }
    animations_fully_cached_ = true;

    if (loaded_now > 0) {
        const auto end = std::chrono::steady_clock::now();
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();
        log::info(std::string("[AssetLibrary] Cached animations for ") + std::to_string(loaded_now) +
                  " additional asset(s) (" + std::to_string(already_cached) +
                  ") in " + std::to_string(elapsed_ms) + "ms");
    }
}

void AssetLibrary::loadAnimationsFor(SDL_Renderer* renderer, const std::unordered_set<std::string>& names) {
    log::debug(std::string("[AssetLibrary] loadAnimationsFor: count=") + std::to_string(names.size()));
    std::size_t idx = 0;
    for (const auto& name : names) {
        // Verbose per-asset line moved to debug level
        log::debug(std::string("[AssetLibrary] (") + std::to_string(idx) + "/" + std::to_string(names.size()) + ") loading '" + name + "'...");
        auto it = info_by_name_.find(name);
        if (it != info_by_name_.end() && it->second) {
            try {
                it->second->loadAnimations(renderer);
            } catch (const std::exception& ex) {
                log::error(std::string("[AssetLibrary] Exception while loading animations for '") + name + "': " + ex.what());
                throw;
            } catch (...) {
                log::error(std::string("[AssetLibrary] Unknown exception while loading animations for '") + name + "'");
                throw;
            }
        } else {
            log::warn(std::string("[AssetLibrary] Missing AssetInfo for '") + name + "'");
        }
        ++idx;
    }
    animations_fully_cached_ = false;
}

bool AssetLibrary::remove(const std::string& name) {
    const bool removed = info_by_name_.erase(name) > 0;
    load_all_from_SRC();
    return removed;
}
