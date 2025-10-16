#include "asset_library.hpp"

#include "core/manifest/manifest_loader.hpp"

#include <iomanip>
#include <iostream>

AssetLibrary::AssetLibrary() {
	load_all_from_SRC();
}

void AssetLibrary::load_all_from_SRC() {
        info_by_name_.clear();

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
    for (auto& [name, info] : info_by_name_) {
        info->loadAnimations(renderer);
    }
}

void AssetLibrary::ensureAllAnimationsLoaded(SDL_Renderer* renderer) {
    if (!renderer) {
        return;
    }

    for (auto& [name, info] : info_by_name_) {
        if (!info) {
            continue;
        }
        if (!info->animations.empty()) {
            continue;
        }
        info->loadAnimations(renderer);
    }
}

void AssetLibrary::loadAnimationsFor(SDL_Renderer* renderer, const std::unordered_set<std::string>& names) {
    for (const auto& name : names) {
        auto it = info_by_name_.find(name);
        if (it != info_by_name_.end() && it->second) {
            it->second->loadAnimations(renderer);
        }
    }
}

bool AssetLibrary::remove(const std::string& name) {
    const bool removed = info_by_name_.erase(name) > 0;
    load_all_from_SRC();
    return removed;
}
