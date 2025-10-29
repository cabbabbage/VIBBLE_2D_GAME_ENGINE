#pragma once

#include <filesystem>

namespace render_pipeline {

struct ScalingProfileBuildOptions {
    std::filesystem::path manifest_root;
    std::filesystem::path output_path;
    double                screen_aspect = 16.0 / 9.0;
};

bool BuildScalingProfiles(const ScalingProfileBuildOptions& options);

}
