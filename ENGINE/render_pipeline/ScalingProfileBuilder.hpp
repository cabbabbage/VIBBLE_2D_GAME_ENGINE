#pragma once

class AssetLibrary;

namespace render_pipeline {

struct ScalingProfileBuildOptions {
    double                screen_aspect = 16.0 / 9.0;
    const AssetLibrary*   asset_library = nullptr;  // Optional: use existing asset library instead of creating new one
};

bool BuildScalingProfiles(const ScalingProfileBuildOptions& options);

}
