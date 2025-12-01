#include "dev_mode/manifest_asset_utils.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <exception>

#include "core/manifest/manifest_loader.hpp"

namespace devmode::manifest_utils {
namespace {
std::string to_lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}
} // namespace

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

} // namespace devmode::manifest_utils
