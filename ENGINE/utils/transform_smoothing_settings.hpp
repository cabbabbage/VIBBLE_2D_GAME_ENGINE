#pragma once

#include <string_view>

#include "transform_smoothing.hpp"

namespace transform_smoothing {

const TransformSmoothingParams& asset_translation_params();
const TransformSmoothingParams& asset_scale_params();
const TransformSmoothingParams& asset_alpha_params();
const TransformSmoothingParams& camera_center_params();
const TransformSmoothingParams& camera_zoom_params();

void reload_from_settings();

}

