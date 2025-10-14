#pragma once

#include <nlohmann/json_fwd.hpp>

#include "render_pipeline/render_asset/shading/ReactiveShadowSettings.hpp"

namespace render_pipeline::shading {

ReactiveShadowSettings reactive_shadow_settings_from_json(const nlohmann::json& json,
                                                          const ReactiveShadowSettings& defaults = ReactiveShadowSettings{});

void assign_reactive_shadow_settings(nlohmann::json& json, const ReactiveShadowSettings& settings);

}  // namespace render_pipeline::shading

