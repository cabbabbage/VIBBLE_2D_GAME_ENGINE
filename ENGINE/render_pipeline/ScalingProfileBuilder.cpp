#include "render/render.hpp"

#include "asset/asset_library.hpp"
#include "asset/asset_info.hpp"
#include "core/manifest/manifest_loader.hpp"
#include "map_generation/map_layers_geometry.hpp"
#include "utils/log.hpp"

#include <memory>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <limits>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace render_pipeline {
namespace {

using nlohmann::json;

struct RoomCandidate {
    double adjusted_area = 0.0;
    bool   is_trail      = false;
    bool   is_spawn      = false;
    std::vector<std::string> assets;
};

struct AssetScaleRange {
    float min_scale = std::numeric_limits<float>::max();
    float max_scale = 0.0f;
};

constexpr double kBaseRatio        = 1.1;
constexpr double kMinScaleClamp    = 0.05;
constexpr double kMaxScaleClamp    = 2.0;
constexpr double kDefaultAspect    = 16.0 / 9.0;
constexpr double kSpawnFallbackMin = kBaseRatio * 0.9;
constexpr double kSpawnFallbackMax = kBaseRatio * 1.05;

struct RoomDimensions {
    int width  = 0;
    int height = 0;
};

int infer_radius_from_dims(int w_min, int w_max, int h_min, int h_max) {
    int diameter = 0;
    diameter = std::max(diameter, std::max(w_min, w_max));
    diameter = std::max(diameter, std::max(h_min, h_max));
    if (diameter <= 0) {
        return 0;
    }
    return std::max(1, diameter / 2);
}

RoomDimensions compute_room_dimensions(const json& entry) {
    RoomDimensions dims{};
    if (!entry.is_object()) {
        return dims;
    }

    int min_w = entry.value("min_width", 64);
    int max_w = entry.value("max_width", min_w);
    int min_h = entry.value("min_height", 64);
    int max_h = entry.value("max_height", min_h);
    int radius = entry.value("radius", -1);

    std::string geometry = entry.value("geometry", std::string{"square"});
    std::string lowered  = geometry;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (lowered == "circle") {
        if (radius <= 0) {
            radius = infer_radius_from_dims(min_w, max_w, min_h, max_h);
        }
        if (radius <= 0) {
            radius = 1;
        }
        min_w = max_w = min_h = max_h = radius * 2;
    }

    dims.width  = std::max(min_w, max_w);
    dims.height = std::max(min_h, max_h);
    if (dims.width <= 0) {
        dims.width = 1;
    }
    if (dims.height <= 0) {
        dims.height = 1;
    }
    return dims;
}

double adjusted_area_for_dimensions(const RoomDimensions& dims, double aspect) {
    if (dims.width <= 0 || dims.height <= 0) {
        return 0.0;
    }
    double width  = static_cast<double>(dims.width);
    double height = static_cast<double>(dims.height);
    const double current_aspect = width / height;
    double target_w             = width;
    double target_h             = height;
    if (aspect <= 0.0) {
        aspect = kDefaultAspect;
    }
    if (current_aspect < aspect) {
        target_w = std::round(height * aspect);
    } else if (current_aspect > aspect) {
        target_h = std::round(width / aspect);
    }
    return std::max(1.0, target_w) * std::max(1.0, target_h);
}

std::vector<std::string> gather_spawn_group_assets(const json& node) {
    const json* groups = nullptr;
    if (node.is_array()) {
        groups = &node;
    } else if (node.is_object()) {
        auto it = node.find("spawn_groups");
        if (it != node.end() && it->is_array()) {
            groups = &(*it);
        }
    }
    if (!groups) {
        return {};
    }

    std::vector<std::string> result;
    for (const auto& group : *groups) {
        if (!group.is_object()) {
            continue;
        }
        auto cand_it = group.find("candidates");
        if (cand_it == group.end() || !cand_it->is_array()) {
            continue;
        }
        for (const auto& candidate : *cand_it) {
            if (!candidate.is_object()) {
                continue;
            }
            std::string name = candidate.value("name", std::string{});
            if (name.empty()) {
                continue;
            }
            if (name == "null") {
                continue;
            }
            result.push_back(std::move(name));
        }
    }
    return result;
}

float safe_scale_factor(const std::shared_ptr<AssetInfo>& info) {
    if (!info) {
        return 1.0f;
    }
    const float factor = info->scale_factor;
    if (!(factor > 0.0f) || !std::isfinite(factor)) {
        return 1.0f;
    }
    return factor;
}

std::vector<float> build_variant_steps(float min_scale, float max_scale) {
    std::vector<float> steps;
    if (!(max_scale > 0.0f) || !std::isfinite(max_scale)) {
        return steps;
    }

    const float clamped_max = std::clamp(max_scale, 0.05f, 1.0f);
    const float clamped_min = std::clamp(min_scale, 0.05f, clamped_max);

    steps.push_back(1.0f);

    float upper_candidate = std::min(clamped_max, 0.98f);
    if (upper_candidate < clamped_min) {
        upper_candidate = clamped_min;
    }

    if (std::fabs(upper_candidate - 1.0f) > 1e-3f) {
        steps.push_back(upper_candidate);
    }

    const float base_for_mid = std::max(clamped_min, std::min(upper_candidate, 0.99f));
    if (base_for_mid > clamped_min + 1e-4f) {
        const float mid = std::clamp(std::sqrt(clamped_min * base_for_mid), clamped_min, base_for_mid);
        if (std::fabs(mid - 1.0f) > 1e-3f && std::fabs(mid - upper_candidate) > 1e-3f) {
            steps.push_back(mid);
        }
    }

    if (std::fabs(clamped_min - 1.0f) > 1e-3f) {
        steps.push_back(clamped_min);
    }
    return steps;
}

std::uint64_t compute_revision(const std::string& asset_name,
                               float min_scale,
                               float max_scale,
                               const std::vector<float>& steps) {
    std::ostringstream oss;
    oss << asset_name << '|' << std::setprecision(6) << min_scale << '|' << max_scale;
    for (float step : steps) {
        oss << '|' << std::setprecision(6) << step;
    }
    const std::string payload = oss.str();
    return static_cast<std::uint64_t>(std::hash<std::string>{}(payload));
}

std::string iso_timestamp_now() {
    const auto now      = std::chrono::system_clock::now();
    const auto time_t   = std::chrono::system_clock::to_time_t(now);
    std::tm    tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &time_t);
#else
    gmtime_r(&time_t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

}

bool BuildScalingProfiles(const ScalingProfileBuildOptions& options) {
    (void)options;
    vibble::log::info("[ScalingProfileBuilder] Dynamic scaling profiles disabled; using fixed variants (100/75/50/25/10).");
    return true;
#if 0
    const double aspect = (options.screen_aspect > 0.0) ? options.screen_aspect : kDefaultAspect;

    vibble::log::info("[ScalingProfileBuilder] Starting scaling profile precomputation...");

    manifest::ManifestData manifest_data;
    try {
        manifest_data = manifest::load_manifest();
    } catch (const std::exception& ex) {
        vibble::log::error(std::string("[ScalingProfileBuilder] Failed to load manifest: ") + ex.what());
        return false;
    }

    std::unique_ptr<AssetLibrary> owned_library;
    const AssetLibrary* library_ptr = options.asset_library;
    if (!library_ptr) {
        owned_library = std::make_unique<AssetLibrary>(true);
        library_ptr = owned_library.get();
    }
    const auto& asset_lookup = library_ptr->all();

    std::unordered_map<std::string, AssetScaleRange> ranges;

    auto process_map = [&](const std::string& map_name, const json& map_entry) {
        if (!map_entry.is_object()) {
            vibble::log::warn(std::string("[ScalingProfileBuilder] Map entry for '") + map_name + "' is not an object.");
            return;
        }

        json rooms_data  = map_entry.value("rooms_data", json::object());
        json trails_data = map_entry.value("trails_data", json::object());
        json map_assets  = map_entry.value("map_assets_data", json::object());
        json layers      = map_entry.value("map_layers", json::array());

        const json* rooms_data_ptr = rooms_data.is_object() ? &rooms_data : nullptr;
        const double min_edge      = map_layers::min_edge_distance_from_map_manifest(map_entry);
        map_layers::LayerRadiiResult radii = map_layers::compute_layer_radii(layers, rooms_data_ptr, min_edge);
        const double map_radius = radii.map_radius;

        std::vector<std::string> map_wide_assets = gather_spawn_group_assets(map_assets);

        std::vector<RoomCandidate> candidates;
        candidates.reserve(rooms_data.size() + trails_data.size());

        auto ingest_section = [&](const json& section, bool is_trail_section) {
            if (!section.is_object()) {
                return;
            }
            for (auto it = section.begin(); it != section.end(); ++it) {
                if (!it.value().is_object()) {
                    continue;
                }
                RoomCandidate candidate;
                candidate.is_trail = is_trail_section;
                candidate.is_spawn = (!is_trail_section) && it.value().value("is_spawn", false);

                const RoomDimensions dims = compute_room_dimensions(it.value());
                candidate.adjusted_area   = adjusted_area_for_dimensions(dims, aspect);

                std::vector<std::string> room_assets = gather_spawn_group_assets(it.value());
                if (!map_wide_assets.empty() && it.value().value("inherits_map_assets", false)) {
                    room_assets.insert(room_assets.end(), map_wide_assets.begin(), map_wide_assets.end());
                }

                std::unordered_set<std::string> unique_assets(room_assets.begin(), room_assets.end());
                candidate.assets.assign(unique_assets.begin(), unique_assets.end());

                candidates.push_back(std::move(candidate));
            }
};

        ingest_section(rooms_data, false);
        ingest_section(trails_data, true);

        if (candidates.empty()) {
            vibble::log::warn(std::string("[ScalingProfileBuilder] Map '") + map_name + "' has no rooms to analyze.");
            return;
        }

        double starting_area = 0.0;
        for (const auto& candidate : candidates) {
            if (candidate.is_spawn) {
                starting_area = candidate.adjusted_area;
                break;
            }
        }
        if (starting_area <= 0.0) {
            starting_area = candidates.front().adjusted_area;
        }
        if (starting_area <= 0.0) {
            starting_area = std::max(1.0, map_radius * map_radius);
        }

        for (const auto& candidate : candidates) {
            if (candidate.assets.empty()) {
                continue;
            }
            double room_scale = kBaseRatio;
            if (candidate.is_trail) {
                room_scale = kBaseRatio * 0.8;
            } else if (candidate.adjusted_area > 0.0 && starting_area > 0.0) {
                room_scale = (candidate.adjusted_area / starting_area) * kBaseRatio;
                room_scale = std::clamp(room_scale, kSpawnFallbackMin, kSpawnFallbackMax);
            }

            if (!(room_scale > 0.0) || !std::isfinite(room_scale)) {
                room_scale = kBaseRatio;
            }

            for (const std::string& asset_name : candidate.assets) {
                auto info_it = asset_lookup.find(asset_name);
                float scale_factor = 1.0f;
                if (info_it != asset_lookup.end()) {
                    scale_factor = safe_scale_factor(info_it->second);
                }
                const float desired_scale = scale_factor / static_cast<float>(room_scale);
                if (!(desired_scale > 0.0f) || !std::isfinite(desired_scale)) {
                    continue;
                }
                auto& range = ranges[asset_name];
                range.min_scale = std::min(range.min_scale, desired_scale);
                range.max_scale = std::max(range.max_scale, desired_scale);
            }
        }
};

    if (manifest_data.maps.is_object()) {
        for (auto it = manifest_data.maps.begin(); it != manifest_data.maps.end(); ++it) {
            process_map(it.key(), it.value());
        }
    }

    const std::string generated_at = iso_timestamp_now();

    if (!manifest_data.raw.is_object()) {
        vibble::log::error("[ScalingProfileBuilder] Manifest is not an object; aborting.");
        return false;
    }

    nlohmann::json& manifest_assets = manifest_data.raw["assets"];
    if (!manifest_assets.is_object()) {
        manifest_assets = json::object();
    }

    for (auto it = manifest_assets.begin(); it != manifest_assets.end(); ++it) {
        if (!it.value().is_object()) {
            continue;
        }

        const auto range_it = ranges.find(it.key());
        if (range_it == ranges.end()) {
            it.value().erase("scaling_profile");
            continue;
        }

        const auto& range = range_it->second;
        if (range.max_scale <= 0.0f || range.min_scale == std::numeric_limits<float>::max()) {
            it.value().erase("scaling_profile");
            continue;
        }

        const float min_scale = std::clamp(range.min_scale, static_cast<float>(kMinScaleClamp), static_cast<float>(kMaxScaleClamp));
        const float max_scale = std::clamp(range.max_scale, static_cast<float>(kMinScaleClamp), static_cast<float>(kMaxScaleClamp));

        std::vector<float> steps = build_variant_steps(min_scale, max_scale);
        json steps_json          = json::array();
        json percent_json        = json::array();
        for (float step : steps) {
            steps_json.push_back(step);
            int percent = static_cast<int>(std::lround(step * 100.0f));
            percent_json.push_back(percent);
        }

        json entry;
        entry["min_scale"]               = min_scale;
        entry["max_scale"]               = max_scale;
        entry["recommended_steps"]       = std::move(steps_json);
        entry["recommended_percentages"] = std::move(percent_json);
        entry["revision"]                = compute_revision(it.key(), min_scale, max_scale, steps);
        entry["screen_aspect"]          = aspect;
        entry["generated_at"]           = generated_at;

        it.value()["scaling_profile"] = std::move(entry);
    }

    try {
        manifest::save_manifest(manifest_data);

        render_pipeline::ScalingLogic::ResetProfileHistory();
        render_pipeline::ScalingLogic::LoadPrecomputedProfiles(true);
    } catch (const std::exception& ex) {
        vibble::log::error(std::string("[ScalingProfileBuilder] Failed to write manifest: ") + ex.what());
        return false;
    }

    manifest_data.assets = manifest_data.raw.at("assets");

    vibble::log::info("[ScalingProfileBuilder] Scaling profile precomputation complete.");
    return true;
#endif
}

}
