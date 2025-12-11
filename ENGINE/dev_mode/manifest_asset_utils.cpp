#include "dev_mode/manifest_asset_utils.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <iostream>

#include "dev_mode/core/manifest_store.hpp"
#include "core/manifest/manifest_loader.hpp"

namespace devmode::manifest_utils {
namespace {
std::string to_lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}
}

bool remove_manifest_asset_entry(const std::string& asset_name, std::ostream* log) {
    if (asset_name.empty()) {
        if (log) {
            *log << "[ManifestAsset] Cannot remove asset with empty name\n";
        }
        return false;
    }

    manifest::ManifestData manifest;

    try {
        manifest = manifest::load_manifest();
    } catch (const std::exception& error) {
        if (log) {
            *log << "[ManifestAsset] Failed to load manifest: " << error.what() << "\n";
        }
        return false;
    }

    auto assets_it = manifest.raw.find("assets");
    if (assets_it == manifest.raw.end() || !assets_it->is_object()) {
        if (log) {
            *log << "[ManifestAsset] Manifest assets section missing or malformed\n";
        }
        return false;
    }

    auto target_it = assets_it->find(asset_name);
    if (target_it == assets_it->end()) {
        const std::string needle = to_lower_copy(asset_name);
        for (auto it = assets_it->begin(); it != assets_it->end(); ++it) {
            if (to_lower_copy(it.key()) == needle) {
                target_it = it;
                break;
            }
        }
    }

    if (target_it == assets_it->end()) {
        if (log) {
            *log << "[ManifestAsset] No manifest asset entry found for '" << asset_name << "'\n";
        }
        return false;
    }

    assets_it->erase(target_it);

    try {
        manifest::save_manifest(manifest);
    } catch (const std::exception& error) {
        if (log) {
            *log << "[ManifestAsset] Failed to save manifest after removing '" << asset_name << "': " << error.what() << "\n";
        }
        return false;
    }

    if (log) {
        *log << "[ManifestAsset] Removed '" << asset_name << "' from manifest assets\n";
    }
    return true;
}

RemoveAssetResult remove_asset_entry(core::ManifestStore* store,
                                     const std::string& asset_name,
                                     std::ostream* log) {
    RemoveAssetResult result{};
    if (asset_name.empty()) {
        if (log) {
            *log << "[ManifestAsset] Cannot remove asset with empty name\n";
        }
        return result;
    }

    if (store) {
        if (auto resolved = store->resolve_asset_name(asset_name)) {
            if (store->remove_asset(*resolved)) {
                result.removed = true;
                result.used_store = true;
            }
        }

        if (!result.removed && store->remove_asset(asset_name)) {
            result.removed = true;
            result.used_store = true;
        }
    }

    if (!result.removed) {
        if (remove_manifest_asset_entry(asset_name, log)) {
            result.removed = true;
            if (store) {
                store->reload();
            }
        } else if (log) {
            *log << "[ManifestAsset] Unable to remove manifest entry for '" << asset_name << "'\n";
        }
    }

    if (store && result.removed) {
        const bool still_exists = store->resolve_asset_name(asset_name).has_value();
        if (still_exists) {
            if (log) {
                *log << "[ManifestAsset] Manifest still contains '" << asset_name
                     << "' after removal attempt\n";
            }
            result.removed = false;
            result.used_store = false;
        }
    }

    return result;
}

}
