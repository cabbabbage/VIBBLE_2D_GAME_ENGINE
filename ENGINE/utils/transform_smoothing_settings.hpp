#pragma once

#include <string_view>

#include "transform_smoothing.hpp"

namespace transform_smoothing {

const TransformSmoothingParams& asset_translation_params();
const TransformSmoothingParams& asset_scale_params();
const TransformSmoothingParams& asset_alpha_params();
const TransformSmoothingParams& camera_center_params();
const TransformSmoothingParams& camera_zoom_params();

void set_asset_translation_params(const TransformSmoothingParams& params);
void set_asset_scale_params(const TransformSmoothingParams& params);
void set_asset_alpha_params(const TransformSmoothingParams& params);
void set_camera_center_params(const TransformSmoothingParams& params);
void set_camera_zoom_params(const TransformSmoothingParams& params);

void reload_from_settings();

}

