#include "asset_library.hpp"

#include "core/manifest/manifest_loader.hpp"

#include <iomanip>
#include <iostream>
#include <chrono>

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
                std::cerr << "[AssetLibrary] Failed to load manifest: " << error.what() << "\n";
                return;
        }

        if (!manifest.assets.is_object()) {
                std::cerr << "[AssetLibrary] Manifest assets section is missing or malformed.\n";
                return;
        }

        int loaded = 0;
        int failed = 0;

        for (auto it = manifest.assets.begin(); it != manifest.assets.end(); ++it) {
                const std::string name = it.key();
                const auto& metadata = it.value();

                if (!metadata.is_object()) {
                        ++failed;
                        std::cerr << "[AssetLibrary] Manifest entry for asset '" << name
                                  << "' is not a JSON object.\n";
                        std::cout << "[AssetLibrary] Loaded: " << loaded
                                  << "   Failed: " << failed
                                  << "   Current: " << std::left << std::setw(20) << name << "\r"
                                  << std::flush;
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
                        std::cerr << "[AssetLibrary] Failed to load asset '" << name
                                  << "': " << error.what() << "\n";
                } catch (...) {
                        ++failed;
                        std::cerr << "[AssetLibrary] Failed to load asset '" << name
                                  << "' due to an unknown error.\n";
                }

                std::cout << "[AssetLibrary] Loaded: " << loaded
                          << "   Failed: " << failed
                          << "   Current: " << std::left << std::setw(20) << name << "\r"
                          << std::flush;
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
    std::cout << "[AssetLibrary] Preloaded animations for " << loaded
              << " asset(s) in " << elapsed_ms << "ms\n";
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
        std::cout << "[AssetLibrary] Cached animations for " << loaded_now
                  << " additional asset(s) (" << already_cached
                  << " already cached) in " << elapsed_ms << "ms\n";
    }
}

void AssetLibrary::loadAnimationsFor(SDL_Renderer* renderer, const std::unordered_set<std::string>& names) {
    std::cout << "[AssetLibrary] loadAnimationsFor: count=" << names.size() << "\n";
    std::size_t idx = 0;
    for (const auto& name : names) {
        std::cout << "[AssetLibrary] (" << idx << "/" << names.size() << ") loading '" << name << "'...\n" << std::flush;
        auto it = info_by_name_.find(name);
        if (it != info_by_name_.end() && it->second) {
            try {
                it->second->loadAnimations(renderer);
            } catch (const std::exception& ex) {
                std::cerr << "[AssetLibrary] Exception while loading animations for '" << name << "': "
                          << ex.what() << "\n" << std::flush;
                throw;
            } catch (...) {
                std::cerr << "[AssetLibrary] Unknown exception while loading animations for '" << name << "'\n" << std::flush;
                throw;
            }
        } else {
            std::cerr << "[AssetLibrary] Missing AssetInfo for '" << name << "'\n";
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
