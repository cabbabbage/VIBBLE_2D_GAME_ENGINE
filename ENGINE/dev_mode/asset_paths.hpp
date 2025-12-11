#pragma once

#include <filesystem>
#include <string>

namespace devmode::asset_paths {

inline std::filesystem::path assets_root_path() {
    static const std::filesystem::path root = (std::filesystem::path("SRC") / "assets").lexically_normal();
    return root;
}

inline std::filesystem::path asset_folder_path(const std::string& name) {
    if (name.empty()) {
        return assets_root_path();
    }
    return (assets_root_path() / name).lexically_normal();
}

inline bool is_protected_asset_root(const std::filesystem::path& path) {
    if (path.empty()) {
        return true;
    }
    const auto normalized = path.lexically_normal();
    static const auto src_root = std::filesystem::path("SRC").lexically_normal();
    static const auto assets_root = assets_root_path();
    return normalized == src_root || normalized == assets_root;
}

}

