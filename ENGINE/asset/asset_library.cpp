#include "asset_library.hpp"
#include "core/manifest/manifest_loader.hpp"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <future>
#include <iomanip>
#include <iostream>
#include <thread>
#include <string>
#include <system_error>
#include <utility>
#include <unordered_set>
#include <vector>
#include "utils/log.hpp"

namespace {

std::filesystem::path assets_root_path() {
        return (std::filesystem::path("SRC") / "assets").lexically_normal();
}

struct AnimationFolderInfo {
        std::string name;
        std::string relative_path;
        int frame_count = 0;
};

std::string to_lower_copy(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
        });
        return value;
}

bool is_reserved_animation_name(const std::string& raw_name) {
        if (raw_name.empty()) {
                return true;
        }
        const std::string name = to_lower_copy(raw_name);
        static const std::unordered_set<std::string> reserved{
            "scaling_profile",
            "scaling-profile",
            "cache",
            "caches",
            "areas",
};
        return reserved.find(name) != reserved.end();
}

int count_png_frames(const std::filesystem::path& folder) {
        int count = 0;
        try {
                if (!std::filesystem::exists(folder) || !std::filesystem::is_directory(folder)) {
                        return 0;
                }
                for (const auto& entry : std::filesystem::directory_iterator(folder)) {
                        if (!entry.is_regular_file()) {
                                continue;
                        }
                        std::string ext = to_lower_copy(entry.path().extension().string());
                        if (ext == ".png") {
                                ++count;
                        }
                }
        } catch (const std::exception& ex) {
                vibble::log::warn(std::string("[AssetLibrary] Unable to enumerate '") + folder.generic_string() + "': " + ex.what());
                return 0;
        } catch (...) {
                vibble::log::warn(std::string("[AssetLibrary] Unknown error enumerating '") + folder.generic_string() + "'");
                return 0;
        }
        return count;
}

std::vector<AnimationFolderInfo> discover_animation_folders(const std::filesystem::path& asset_dir) {
        std::vector<AnimationFolderInfo> result;
        try {
                if (!std::filesystem::exists(asset_dir) || !std::filesystem::is_directory(asset_dir)) {
                        return result;
                }
        } catch (...) {
                return result;
        }

        std::unordered_set<std::string> seen;

        const int root_frames = count_png_frames(asset_dir);
        if (root_frames > 0) {
                seen.insert("default");
                result.push_back(AnimationFolderInfo{"default", "", root_frames});
        }

        try {
                for (const auto& entry : std::filesystem::directory_iterator(asset_dir)) {
                        if (!entry.is_directory()) {
                                continue;
                        }
                        std::string name = entry.path().filename().string();
                        if (name.empty()) {
                                continue;
                        }
                        if (name.front() == '.' || is_reserved_animation_name(name)) {
                                continue;
                        }
                        const int frames = count_png_frames(entry.path());
                        if (frames <= 0) {
                                continue;
                        }
                        if (seen.insert(name).second) {
                                result.push_back(AnimationFolderInfo{name, name, frames});
                        }
                }
        } catch (const std::exception& ex) {
                vibble::log::warn(std::string("[AssetLibrary] Failed to enumerate animations under '") + asset_dir.generic_string() + "': " + ex.what());
        } catch (...) {
                vibble::log::warn(std::string("[AssetLibrary] Unknown error enumerating animations under '") + asset_dir.generic_string() + "'");
        }

        std::sort(result.begin(), result.end(), [](const AnimationFolderInfo& lhs, const AnimationFolderInfo& rhs) {
                return lhs.name < rhs.name;
        });
        return result;
}

bool ensure_start_animation(nlohmann::json& metadata) {
        auto animations_it = metadata.find("animations");
        if (animations_it == metadata.end() || !animations_it->is_object()) {
                        return false;
        }
        const auto& animations = *animations_it;
        const auto is_valid = [&](const std::string& candidate) -> bool {
                if (candidate.empty()) {
                        return false;
                }
                if (is_reserved_animation_name(candidate)) {
                        return false;
                }
                auto it = animations.find(candidate);
                return it != animations.end() && it->is_object();
};

        if (metadata.contains("start") && metadata["start"].is_string()) {
                const std::string existing = metadata["start"].get<std::string>();
                if (is_valid(existing)) {
                        return false;
                }
        }

        const auto select = [&]() -> std::string {
                if (is_valid("default")) {
                        return "default";
                }
                if (is_valid("idle")) {
                        return "idle";
                }
                for (auto it = animations.begin(); it != animations.end(); ++it) {
                        if (is_valid(it.key())) {
                                return it.key();
                        }
                }
                return {};
};

        std::string replacement = select();
        if (replacement.empty()) {
                return false;
        }
        metadata["start"] = replacement;
        return true;
}

bool ensure_animation_metadata(const std::string& asset_name,
                               nlohmann::json& metadata,
                               const std::filesystem::path& assets_root) {
        const auto asset_dir = (assets_root / asset_name).lexically_normal();
        const auto folders = discover_animation_folders(asset_dir);
        if (folders.empty()) {
                return false;
        }

        bool mutated = false;
        if (!metadata.contains("animations") || !metadata["animations"].is_object()) {
                metadata["animations"] = nlohmann::json::object();
                mutated = true;
        }
        nlohmann::json& animations = metadata["animations"];

        for (const auto& folder : folders) {
                nlohmann::json& slot = animations[folder.name];
                if (!slot.is_object()) {
                        slot = nlohmann::json::object();
                        mutated = true;
                }

                if (!slot.contains("source") || !slot["source"].is_object()) {
                        slot["source"] = nlohmann::json::object();
                        mutated = true;
                }
                nlohmann::json& source = slot["source"];
                bool source_mutated = false;
                if (!source.contains("kind") || !source["kind"].is_string() || source["kind"].get<std::string>().empty()) {
                        source["kind"] = "folder";
                        source_mutated = true;
                }
                const std::string desired_path = folder.relative_path;
                if (!source.contains("path") || !source["path"].is_string() || source["path"].get<std::string>() != desired_path) {
                        source["path"] = desired_path;
                        source_mutated = true;
                }
                if (source_mutated) {
                        mutated = true;
                }

                if (!slot.contains("loop") || !slot["loop"].is_boolean()) {
                        slot["loop"] = true;
                        mutated = true;
                }
                if (!slot.contains("locked") || !slot["locked"].is_boolean()) {
                        slot["locked"] = false;
                        mutated = true;
                }
        }

        mutated |= ensure_start_animation(metadata);
        return mutated;
}

bool ensure_manifest_entry_shape(const std::string& asset_name,
                                 nlohmann::json& metadata,
                                 const std::filesystem::path& assets_root) {
        bool mutated = false;
        if (!metadata.is_object()) {
                metadata = nlohmann::json::object();
                mutated = true;
        }
        if (!metadata.contains("asset_name") || !metadata["asset_name"].is_string() || metadata["asset_name"].get<std::string>().empty()) {
                metadata["asset_name"] = asset_name;
                mutated = true;
        }
        const auto default_dir = (assets_root / asset_name).lexically_normal().generic_string();
        if (!metadata.contains("asset_directory") || !metadata["asset_directory"].is_string() || metadata["asset_directory"].get<std::string>().empty()) {
                metadata["asset_directory"] = default_dir;
                mutated = true;
        }
        mutated |= ensure_animation_metadata(asset_name, metadata, assets_root);
        return mutated;
}

std::vector<std::string> discover_asset_directories(const std::filesystem::path& assets_root) {
        std::vector<std::string> names;
        try {
                if (!std::filesystem::exists(assets_root) || !std::filesystem::is_directory(assets_root)) {
                        return names;
                }
                for (const auto& entry : std::filesystem::directory_iterator(assets_root)) {
                        if (!entry.is_directory()) {
                                continue;
                        }
                        const std::string name = entry.path().filename().string();
                        if (!name.empty()) {
                                names.push_back(name);
                        }
                }
        } catch (const std::exception& ex) {
                vibble::log::warn(std::string("[AssetLibrary] Failed to enumerate assets root '") + assets_root.generic_string() + "': " + ex.what());
                names.clear();
        }
        std::sort(names.begin(), names.end());
        names.erase(std::unique(names.begin(), names.end()), names.end());
        return names;
}

}

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
                vibble::log::error(std::string("[AssetLibrary] Failed to load manifest: ") + error.what());
                return;
        }

        const auto manifest_path = std::filesystem::absolute(std::filesystem::path(manifest::manifest_path()));
        vibble::log::info(std::string("[FrameData] Loading animations manifest from ") + manifest_path.generic_string());

        if (!manifest.assets.is_object()) {
                vibble::log::error("[AssetLibrary] Manifest assets section is missing or malformed.");
                return;
        }

        auto& raw_assets = manifest.raw["assets"];
        const auto assets_root = assets_root_path();
        bool manifest_dirty = false;

        for (auto it = raw_assets.begin(); it != raw_assets.end(); ++it) {
                manifest_dirty |= ensure_manifest_entry_shape(it.key(), it.value(), assets_root);
        }

        const auto discovered_assets = discover_asset_directories(assets_root);
        if (discovered_assets.empty()) {
                std::error_code ec;
                const bool assets_root_exists = std::filesystem::exists(assets_root, ec);
                if (!assets_root_exists || ec) {
                        vibble::log::warn(std::string("[AssetLibrary] Assets root '") + assets_root.generic_string() + "' is missing or inaccessible.");
                }
        } else {
                for (const auto& asset_name : discovered_assets) {
                        nlohmann::json& metadata = raw_assets[asset_name];
                        manifest_dirty |= ensure_manifest_entry_shape(asset_name, metadata, assets_root);
                }
        }

        manifest.assets = raw_assets;
        if (manifest_dirty) {
                try {
                        manifest::save_manifest(manifest);
                        vibble::log::info("[AssetLibrary] Manifest assets section synchronized with SRC/assets contents.");
                } catch (const std::exception& error) {
                        vibble::log::warn(std::string("[AssetLibrary] Failed to persist manifest sync: ") + error.what());
                }
        }

        int loaded = 0;
        int failed = 0;
        const auto start_ms = std::chrono::steady_clock::now();

        struct AssetBuildJob {
                std::string name;
                nlohmann::json metadata;
};
        std::vector<AssetBuildJob> work_items;
        work_items.reserve(manifest.assets.size());

        for (auto it = manifest.assets.begin(); it != manifest.assets.end(); ++it) {
                const std::string name = it.key();
                const auto& metadata = it.value();

                if (!metadata.is_object()) {
                        ++failed;
                        vibble::log::warn(std::string("[AssetLibrary] Manifest entry for asset '") + name + "' is not a JSON object.");
                        continue;
                }

                work_items.push_back(AssetBuildJob{name, metadata});
        }

        if (!work_items.empty()) {
                const unsigned int hardware_threads = std::max(1u, std::thread::hardware_concurrency());
                const std::size_t worker_count = std::min(work_items.size(), static_cast<std::size_t>(hardware_threads));
                const std::size_t slice_size = (work_items.size() + worker_count - 1) / worker_count;

                struct WorkerResult {
                        int loaded = 0;
                        int failed = 0;
                        std::vector<std::pair<std::string, std::shared_ptr<AssetInfo>>> assets;
};

                std::vector<std::future<WorkerResult>> futures;
                futures.reserve(worker_count);

                for (std::size_t worker_index = 0; worker_index < worker_count; ++worker_index) {
                        const std::size_t start_index = worker_index * slice_size;
                        if (start_index >= work_items.size()) {
                                break;
                        }
                        const std::size_t end_index = std::min(work_items.size(), start_index + slice_size);
                        futures.push_back(std::async(std::launch::async,
                                                     [start_index, end_index, &work_items]() -> WorkerResult {
                                                             WorkerResult result;
                                                             result.assets.reserve(end_index - start_index);
                                                             for (std::size_t idx = start_index; idx < end_index; ++idx) {
                                                                     const auto& item = work_items[idx];
                                                                     try {
                                                                             const bool has_metadata = item.metadata.is_object() && !item.metadata.empty();
                                                                             auto info = AssetInfo::from_manifest_entry(
                                                                                 item.name,
                                                                                 has_metadata ? item.metadata : nlohmann::json::object());
                                                                             result.assets.emplace_back(item.name, std::move(info));
                                                                             ++result.loaded;
                                                                     } catch (const std::exception& error) {
                                                                             ++result.failed;
                                                                             vibble::log::warn(std::string("[AssetLibrary] Failed to load asset '") +
                                                                                               item.name + "': " + error.what());
                                                                     } catch (...) {
                                                                             ++result.failed;
                                                                             vibble::log::warn(std::string("[AssetLibrary] Failed to load asset '") +
                                                                                               item.name + "' due to an unknown error.");
                                                                     }
                                                             }
                                                             return result;
                                                     }));
                }

                for (auto& future : futures) {
                        WorkerResult result = future.get();
                        loaded += result.loaded;
                        failed += result.failed;
                        for (auto& entry : result.assets) {
                                info_by_name_[entry.first] = std::move(entry.second);
                        }
                }
        }
        const auto end_ms = std::chrono::steady_clock::now();
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_ms - start_ms).count();
        vibble::log::info(std::string("[AssetLibrary] Loaded ") + std::to_string(info_by_name_.size()) + " assets (ok=" + std::to_string(loaded) + ", failed=" + std::to_string(failed) + ") in " + std::to_string(elapsed_ms) + "ms");
}

void AssetLibrary::add_asset(const std::string& name, const nlohmann::json& metadata) {
    if (info_by_name_.count(name)) {

        return;
    }

    try {
        std::shared_ptr<AssetInfo> info = AssetInfo::from_manifest_entry(name, metadata);
        info_by_name_[name] = info;
        vibble::log::info(std::string("[AssetLibrary] Added asset '") + name + "' to library");
    } catch (const std::exception& error) {
        vibble::log::error(std::string("[AssetLibrary] Failed to add asset '") + name + "': " + error.what());
    } catch (...) {
        vibble::log::error(std::string("[AssetLibrary] Failed to add asset '") + name + "' due to an unknown error.");
    }
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
    vibble::log::info(std::string("[AssetLibrary] Preloaded animations for ") + std::to_string(loaded) + " asset(s) in " + std::to_string(elapsed_ms) + "ms");
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
        vibble::log::info(std::string("[AssetLibrary] Cached animations for ") + std::to_string(loaded_now) + " additional asset(s) (" + std::to_string(already_cached) + ") in " + std::to_string(elapsed_ms) + "ms");
    }
}

void AssetLibrary::loadAnimationsFor(SDL_Renderer* renderer, const std::unordered_set<std::string>& names) {
    vibble::log::debug(std::string("[AssetLibrary] loadAnimationsFor: count=") + std::to_string(names.size()));
    std::size_t idx = 0;
    for (const auto& name : names) {

        vibble::log::debug(std::string("[AssetLibrary] (") + std::to_string(idx) + "/" + std::to_string(names.size()) + ") loading '" + name + "'...");
        auto it = info_by_name_.find(name);
        if (it != info_by_name_.end() && it->second) {
            try {
                it->second->loadAnimations(renderer);
            } catch (const std::exception& ex) {
                vibble::log::error(std::string("[AssetLibrary] Exception while loading animations for '") + name + "': " + ex.what());
                throw;
            } catch (...) {
                vibble::log::error(std::string("[AssetLibrary] Unknown exception while loading animations for '") + name + "'");
                throw;
            }
        } else {
            vibble::log::warn(std::string("[AssetLibrary] Missing AssetInfo for '") + name + "'");
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
