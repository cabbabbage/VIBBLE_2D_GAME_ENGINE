#pragma once

#include <filesystem>

class AssetLibrary;

namespace render_pipeline {

struct ScalingProfileBuildOptions {
    std::filesystem::path manifest_root;
    std::filesystem::path output_path;
    double                screen_aspect = 16.0 / 9.0;
    const AssetLibrary*   asset_library = nullptr;  // Optional: use existing asset library instead of creating new one
};

bool BuildScalingProfiles(const ScalingProfileBuildOptions& options);

}
