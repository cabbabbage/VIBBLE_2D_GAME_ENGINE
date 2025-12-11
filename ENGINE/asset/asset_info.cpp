#include "asset_info.hpp"

#include "asset/asset_types.hpp"
#include "asset/animation_loader.hpp"
#include "asset_info_methods/asset_child_loader.hpp"
#include "asset_info_methods/lighting_loader.hpp"
#include "utils/cache_manager.hpp"
#include "utils/rebuild_queue.hpp"
#include "core/manifest/manifest_loader.hpp"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <cstdlib>
#include <random>
#include <limits>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <system_error>
#include <utility>
#include <unordered_set>

#include "dev_mode/core/manifest_store.hpp"
#include "utils/grid.hpp"

namespace fs = std::filesystem;

namespace {

struct CanvasMetrics {
    int width = 0;
    int height = 0;
};

std::vector<std::string> parse_string_array(const nlohmann::json& json_value) {
    std::vector<std::string> values;
    if (!json_value.is_array()) {
        return values;
    }
    values.reserve(json_value.size());
    for (const auto& entry : json_value) {
        if (entry.is_string()) {
            auto str = entry.get<std::string>();
            if (!str.empty()) {
                values.push_back(std::move(str));
            }
        }
    }
    return values;
}

AnimationChildFrameData parse_async_child_frame(const nlohmann::json& entry) {
    AnimationChildFrameData data{};
    data.child_index = -1;

    auto read_bool = [](const nlohmann::json& v, bool fallback) {
        if (v.is_boolean()) return v.get<bool>();
        if (v.is_number_integer()) return v.get<int>() != 0;
        return fallback;
};

    if (entry.is_object()) {
        try { data.dx = static_cast<int>(entry.value("dx", 0)); } catch (...) { data.dx = 0; }
        try { data.dy = static_cast<int>(entry.value("dy", 0)); } catch (...) { data.dy = 0; }
        if (entry.contains("degree") && entry["degree"].is_number()) {
            try { data.degree = static_cast<float>(entry["degree"].get<double>()); } catch (...) { data.degree = 0.0f; }
        } else if (entry.contains("rotation") && entry["rotation"].is_number()) {
            try { data.degree = static_cast<float>(entry["rotation"].get<double>()); } catch (...) { data.degree = 0.0f; }
        }
        data.visible = read_bool(entry.value("visible", true), true);
        data.render_in_front = read_bool(entry.value("render_in_front", true), true);
        return data;
    }

    if (entry.is_array() && !entry.empty()) {
        if (entry.size() >= 1 && entry[0].is_number()) { try { data.dx = entry[0].get<int>(); } catch (...) { data.dx = 0; } }
        if (entry.size() >= 2 && entry[1].is_number()) { try { data.dy = entry[1].get<int>(); } catch (...) { data.dy = 0; } }
        if (entry.size() >= 3 && entry[2].is_number()) { try { data.degree = static_cast<float>(entry[2].get<double>()); } catch (...) { data.degree = 0.0f; } }
        if (entry.size() >= 4) data.visible = read_bool(entry[3], true);
        if (entry.size() >= 5) data.render_in_front = read_bool(entry[4], true);
    }

    return data;
}

nlohmann::json encode_async_child_frames(const std::vector<AnimationChildFrameData>& frames) {
    nlohmann::json arr = nlohmann::json::array();
    auto& out = arr.get_ref<nlohmann::json::array_t&>();
    out.reserve(frames.size());
    for (const auto& frame : frames) {
        nlohmann::json obj = nlohmann::json::object();
        obj["dx"] = frame.dx;
        obj["dy"] = frame.dy;
        obj["degree"] = frame.degree;
        obj["visible"] = frame.visible;
        obj["render_in_front"] = frame.render_in_front;
        out.push_back(std::move(obj));
    }
    return arr;
}

std::vector<AsyncChildDefinition> parse_async_children(const nlohmann::json& data) {
    std::vector<AsyncChildDefinition> result;
    if (!data.contains("async_children") || !data["async_children"].is_array()) {
        return result;
    }

    std::unordered_set<std::string> seen_names;
    for (const auto& entry : data["async_children"]) {
        if (!entry.is_object()) continue;
        AsyncChildDefinition def;
        def.name = entry.value("name", std::string{});
        if (def.name.empty()) continue;
        if (!seen_names.insert(def.name).second) continue;
        def.asset = entry.value("asset", entry.value("child", std::string{}));
        def.animation = entry.value("animation", std::string{});
        if (entry.contains("frames") && entry["frames"].is_array()) {
            for (const auto& f : entry["frames"]) {
                def.frames.push_back(parse_async_child_frame(f));
            }
        }
        if (def.valid()) {
            result.push_back(std::move(def));
        }
    }
    return result;
}

constexpr int kLightTextureCacheVersion = 3;

std::string light_signature(const LightSource& light) {
    std::ostringstream oss;
    oss << light.radius << '|'
        << light.fall_off << '|'
        << light.flare << '|'
        << light.intensity << '|'
        << light.flicker_speed << '|'
        << light.flicker_smoothness;
    return oss.str();
}

void destroy_light_textures(std::vector<LightSource>& lights) {
    for (auto& light : lights) {
        if (light.texture) {
            SDL_DestroyTexture(light.texture);
            light.texture = nullptr;
        }
        light.cached_w = 0;
        light.cached_h = 0;
    }
}

bool load_light_cache_metadata(const fs::path& meta_path,
                               std::vector<std::string>& out_signatures) {
    out_signatures.clear();
    nlohmann::json meta;
    if (!CacheManager::load_metadata(meta_path.generic_string(), meta)) {
        return false;
    }
    if (!meta.is_object()) {
        return false;
    }
    if (meta.value("version", -1) != kLightTextureCacheVersion) {
        return false;
    }
    auto it = meta.find("signatures");
    if (it == meta.end() || !it->is_array()) {
        return false;
    }
    for (const auto& entry : *it) {
        if (!entry.is_string()) {
            return false;
        }
        out_signatures.push_back(entry.get<std::string>());
    }
    return true;
}

bool save_light_cache_metadata(const fs::path& meta_path,
                               const std::vector<std::string>& signatures) {
    nlohmann::json meta;
    meta["version"]    = kLightTextureCacheVersion;
    meta["signatures"] = signatures;
    return CacheManager::save_metadata(meta_path.generic_string(), meta);
}

bool load_cached_light_textures(const fs::path& cache_dir,
                                SDL_Renderer* renderer,
                                std::vector<LightSource>& lights) {
    if (!renderer) {
        return false;
    }
    for (std::size_t i = 0; i < lights.size(); ++i) {
        const fs::path png_path = cache_dir / ("light_" + std::to_string(i) + ".png");
        if (!fs::exists(png_path)) {
            destroy_light_textures(lights);
            return false;
        }
        SDL_Surface* surface = CacheManager::load_surface(png_path.generic_string());
        if (!surface) {
            destroy_light_textures(lights);
            return false;
        }
        SDL_Texture* tex = CacheManager::surface_to_texture(renderer, surface);
        const int w = surface->w;
        const int h = surface->h;
        SDL_FreeSurface(surface);
        if (!tex) {
            destroy_light_textures(lights);
            return false;
        }
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        lights[i].texture = tex;
        lights[i].cached_w = w;
        lights[i].cached_h = h;
    }
    return true;
}

bool try_load_cached_lights(const fs::path& cache_dir,
                            SDL_Renderer* renderer,
                            std::vector<LightSource>& lights,
                            const std::vector<std::string>& signatures) {
    const fs::path meta_path = cache_dir / "metadata.json";
    std::vector<std::string> cached_signatures;
    if (!load_light_cache_metadata(meta_path, cached_signatures)) {
        return false;
    }
    if (cached_signatures != signatures) {
        return false;
    }
    return load_cached_light_textures(cache_dir, renderer, lights);
}

bool regenerate_lights_via_python(const std::string& asset_name) {
    if (asset_name.empty()) {
        return false;
    }

    vibble::RebuildQueueCoordinator coordinator;
    coordinator.request_light(asset_name);

#if defined(_WIN32)
    const std::string prefix =
        "set \"PATH=%PATH%;C:\\Program Files\\NVIDIA GPU Computing Toolkit\\CUDA\\v12.0\\bin\" && ";
#else
    const std::string prefix;
#endif

    std::cout << "[AssetInfo] Regenerating lights via light_tool.py for '"
              << asset_name << "'\n";
    if (!coordinator.run_light_tool(prefix)) {
        std::cerr << "[AssetInfo] light_tool.py failed for '" << asset_name << "'\n";
        return false;
    }

    return true;
}

float read_float_field(const nlohmann::json& data, const char* key, float fallback) {
    if (!key) {
        return fallback;
    }
    try {
        auto it = data.find(key);
        if (it == data.end()) {
            return fallback;
        }
        if (it->is_number_float() || it->is_number_integer()) {
            return static_cast<float>(it->get<double>());
        }
        if (it->is_string()) {
            const std::string text = it->get<std::string>();
            if (!text.empty()) {
                try {
                    size_t consumed = 0;
                    float parsed = std::stof(text, &consumed);
                    if (consumed > 0) {
                        return parsed;
                    }
                } catch (...) {
                }
            }
        }
    } catch (...) {
    }
    return fallback;
}

std::filesystem::path assets_root_for(const std::string& asset_name) {
    std::filesystem::path base = std::filesystem::path("SRC") / "assets";
    if (!asset_name.empty()) {
        base /= asset_name;
    }
    return base.lexically_normal();
}

bool path_starts_with_src(const std::filesystem::path& path) {
    if (path.empty()) {
        return false;
    }
    const std::string generic = path.lexically_normal().generic_string();
    return generic == "SRC" || generic.rfind("SRC/", 0) == 0;
}

std::string prefer_assets_directory(const std::string& configured, const std::string& asset_name) {
    const auto preferred = assets_root_for(asset_name);

    if (configured.empty()) {
        return preferred.generic_string();
    }

    std::filesystem::path candidate = std::filesystem::path(configured).lexically_normal();
    if (!path_starts_with_src(candidate)) {
        return candidate.generic_string();
    }

    if (candidate == preferred) {
        return candidate.generic_string();
    }

    std::error_code ec;
    const bool preferred_exists = std::filesystem::exists(preferred, ec);
    ec.clear();
    const bool candidate_exists = std::filesystem::exists(candidate, ec);
    ec.clear();
    if (candidate_exists) {
        return candidate.generic_string();
    }

    if (preferred_exists) {
        return preferred.generic_string();
    }

    return preferred.generic_string();
}

std::string derive_asset_directory(const nlohmann::json& data, const std::string& fallback) {
    try {
        if (data.contains("asset_directory") && data["asset_directory"].is_string()) {
            auto value = data["asset_directory"].get<std::string>();
            if (!value.empty()) {
                return value;
            }
        }

        auto anims_it = data.find("animations");
        if (anims_it != data.end() && anims_it->is_object()) {
            for (auto it = anims_it->begin(); it != anims_it->end(); ++it) {
                if (!it.value().is_object()) {
                    continue;
                }
                const auto& anim_json = it.value();
                if (anim_json.contains("source") && anim_json["source"].is_object()) {
                    std::string path = anim_json["source"].value("path", std::string{});
                    if (!path.empty()) {
                        std::filesystem::path p(path);
                        if (!p.empty()) {
                            if (p.has_filename()) {
                                p = p.parent_path();
                            }
                            return p.string();
                        }
                    }
                } else if (anim_json.contains("frames_path") && anim_json["frames_path"].is_string()) {
                    std::filesystem::path p = std::filesystem::path(fallback) / anim_json["frames_path"].get<std::string>();
                    if (p.has_parent_path()) {
                        return p.parent_path().string();
                    }
                }
            }
        }
    } catch (...) {
    }

    return fallback;
}

const nlohmann::json* locate_animation_container(const nlohmann::json& root) {
    if (!root.is_object()) {
        return nullptr;
    }

    auto animations_it = root.find("animations");
    if (animations_it != root.end() && animations_it->is_object()) {
        return &(*animations_it);
    }

    return nullptr;
}

const nlohmann::json* locate_animation_payloads(const nlohmann::json& root) {
    if (!root.is_object()) {
        return nullptr;
    }

    if (const auto* container = locate_animation_container(root)) {
        auto nested = container->find("animations");
        if (nested != container->end() && nested->is_object()) {
            return &(*nested);
        }
        return container;
    }

    return &root;
}

bool extract_start_value(const nlohmann::json& root, std::string& out) {
    if (!root.is_object()) {
        return false;
    }

    if (const auto* container = locate_animation_container(root)) {
        auto start_it = container->find("start");
        if (start_it != container->end() && start_it->is_string()) {
            std::string candidate = start_it->get<std::string>();
            if (!candidate.empty()) {
                out = std::move(candidate);
                return true;
            }
        }
    }

    auto start_it = root.find("start");
    if (start_it != root.end() && start_it->is_string()) {
        std::string candidate = start_it->get<std::string>();
        if (!candidate.empty()) {
            out = std::move(candidate);
            return true;
        }
    }

    if (const auto* payloads = locate_animation_payloads(root)) {
        auto nested_start = payloads->find("start");
        if (nested_start != payloads->end() && nested_start->is_string()) {
            std::string candidate = nested_start->get<std::string>();
            if (!candidate.empty()) {
                out = std::move(candidate);
                return true;
            }
        }
    }

    return false;
}

inline CanvasMetrics canvas_metrics_for(const AssetInfo& info) {
    CanvasMetrics metrics;
    metrics.width = std::max(info.original_canvas_width, 0);
    metrics.height = std::max(info.original_canvas_height, 0);
    return metrics;
}

inline CanvasMetrics metrics_from_json(const nlohmann::json& space) {
    CanvasMetrics metrics;
    metrics.width = std::max(space.value("canvas_width", 0), 0);
    metrics.height = std::max(space.value("canvas_height", 0), 0);
    return metrics;
}

inline float sanitize_scale(float scale) {
    if (!(scale > 0.0f) || !std::isfinite(scale)) {
        return 1.0f;
    }
    return scale;
}

inline int compute_scaled_dimension(int dimension, float factor) {
    if (dimension <= 0) return 0;
    double value = static_cast<double>(dimension) * static_cast<double>(factor);
    long long rounded = std::llround(value);
    if (rounded < 0) {
        return 0;
    }
    if (rounded > static_cast<long long>(std::numeric_limits<int>::max())) {
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(rounded);
}

inline SDL_Point canonical_anchor(const CanvasMetrics& canvas) {
    SDL_Point anchor{0, 0};
    anchor.x = (canvas.width > 0) ? canvas.width / 2 : 0;
    anchor.y = canvas.height;
    return anchor;
}

inline SDL_Point scaled_anchor_point(const CanvasMetrics& canvas, float scale) {
    SDL_Point anchor{0, 0};
    const int scaled_w = compute_scaled_dimension(canvas.width, scale);
    const int scaled_h = compute_scaled_dimension(canvas.height, scale);
    anchor.x = (scaled_w > 0) ? scaled_w / 2 : 0;
    anchor.y = scaled_h;
    return anchor;
}

inline int unscale_dimension(int dimension, float scale) {
    if (!(scale > 0.0f) || !std::isfinite(scale)) {
        return dimension;
    }
    if (dimension <= 0) {
        return 0;
    }
    const double value = static_cast<double>(dimension) / static_cast<double>(scale);
    const long long rounded = std::llround(value);
    if (rounded < 0) {
        return 0;
    }
    if (rounded > static_cast<long long>(std::numeric_limits<int>::max())) {
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(rounded);
}

inline nlohmann::json encode_canonical_points(const std::vector<Area::Point>& points,
                                              SDL_Point anchor,
                                              float scale) {
    nlohmann::json arr = nlohmann::json::array();
    auto& out = arr.get_ref<nlohmann::json::array_t&>();
    out.reserve(points.size());
    for (const auto& p : points) {
        const long long dx_scaled = static_cast<long long>(p.x) - static_cast<long long>(anchor.x);
        const long long dy_scaled = static_cast<long long>(p.y) - static_cast<long long>(anchor.y);
        const int canonical_x = static_cast<int>(std::llround(static_cast<double>(dx_scaled) / scale));
        const int canonical_y = static_cast<int>(std::llround(static_cast<double>(dy_scaled) / scale));
        out.push_back({ {"x", canonical_x}, {"y", canonical_y} });
    }
    return arr;
}

inline std::vector<Area::Point> decode_canonical_points(const nlohmann::json& points,
                                                        SDL_Point anchor,
                                                        float scale) {
    std::vector<Area::Point> decoded;
    if (!points.is_array()) return decoded;
    decoded.reserve(points.size());
    for (const auto& entry : points) {
        if (!entry.is_object()) continue;
        const int canonical_x = entry.value("x", 0);
        const int canonical_y = entry.value("y", 0);
        const long long scaled_dx = static_cast<long long>(std::llround(static_cast<double>(canonical_x) * scale));
        const long long scaled_dy = static_cast<long long>(std::llround(static_cast<double>(canonical_y) * scale));
        const long long world_x = static_cast<long long>(anchor.x) + scaled_dx;
        const long long world_y = static_cast<long long>(anchor.y) + scaled_dy;
        Area::Point p{};
        if (world_x < static_cast<long long>(std::numeric_limits<int>::min())) {
            p.x = std::numeric_limits<int>::min();
        } else if (world_x > static_cast<long long>(std::numeric_limits<int>::max())) {
            p.x = std::numeric_limits<int>::max();
        } else {
            p.x = static_cast<int>(world_x);
        }
        if (world_y < static_cast<long long>(std::numeric_limits<int>::min())) {
            p.y = std::numeric_limits<int>::min();
        } else if (world_y > static_cast<long long>(std::numeric_limits<int>::max())) {
            p.y = std::numeric_limits<int>::max();
        } else {
            p.y = static_cast<int>(world_y);
        }
        decoded.push_back(p);
    }
    return decoded;
}

}

SDL_Point AssetInfo::AreaCodec::scaled_anchor(const AssetInfo& info,
                                             std::optional<float> scale_override) {
    const float scale = sanitize_scale(scale_override.value_or(info.scale_factor));
    CanvasMetrics canvas = canvas_metrics_for(info);
    return scaled_anchor_point(canvas, scale);
}

nlohmann::json AssetInfo::AreaCodec::encode_entry(
    const AssetInfo& info,
    const Area& area,
    const std::string& final_type,
    const std::string& final_kind,
    std::optional<AssetInfo::NamedArea::RenderFrame> frame) {
    nlohmann::json entry = nlohmann::json::object();
    entry["name"] = area.get_name();
    if (!final_type.empty()) {
        entry["type"] = final_type;
    }
    if (!final_kind.empty()) {
        entry["kind"] = final_kind;
    }
    entry["schema_version"] = 2;

    if (!frame) {
        for (const auto& na : info.areas) {
            if (!na.area) continue;
            if (na.area->get_name() == area.get_name() && na.render_frame) {
                frame = na.render_frame;
                break;
            }
        }
    }

    const float info_scale = sanitize_scale(info.scale_factor);
    const float save_scale = sanitize_scale(frame ? frame->pixel_scale : info_scale);
    CanvasMetrics canonical_canvas = canvas_metrics_for(info);
    nlohmann::json coordinate_space = {
        {"origin", "bottom_center"},
        {"scale_at_save", save_scale}
};

    SDL_Point render_anchor{0, 0};
    if (frame && frame->is_valid()) {
        coordinate_space["kind"] = "render_space";
        coordinate_space["canvas_width"] = frame->width;
        coordinate_space["canvas_height"] = frame->height;
        coordinate_space["pivot"] = {
            {"x", frame->pivot_x},
            {"y", frame->pivot_y}
};

        if (canonical_canvas.width <= 0) {
            canonical_canvas.width = unscale_dimension(frame->width, save_scale);
        }
        if (canonical_canvas.height <= 0) {
            canonical_canvas.height = unscale_dimension(frame->height, save_scale);
        }
        render_anchor.x = frame->pivot_x;
        render_anchor.y = frame->pivot_y;
    } else {
        coordinate_space["kind"] = "canonical";
        coordinate_space["canvas_width"] = canonical_canvas.width;
        coordinate_space["canvas_height"] = canonical_canvas.height;
        render_anchor = scaled_anchor_point(canonical_canvas, save_scale);
    }

    entry["coordinate_space"] = coordinate_space;

    const SDL_Point canonical_anchor_point = canonical_anchor(canonical_canvas);
    entry["anchor"] = { {"x", canonical_anchor_point.x}, {"y", canonical_anchor_point.y} };
    entry["points"] = encode_canonical_points(area.get_points(), render_anchor, save_scale);
    entry["resolution"] = area.resolution();
    return entry;
}

std::optional<AssetInfo::NamedArea>
AssetInfo::AreaCodec::decode_entry(const AssetInfo& info, const nlohmann::json& entry) {
    if (!entry.is_object()) {
        return std::nullopt;
    }
    const std::string name = entry.value("name", std::string{});
    if (name.empty()) {
        return std::nullopt;
    }
    if (!entry.contains("points") || !entry["points"].is_array()) {
        return std::nullopt;
    }
    if (!entry.contains("coordinate_space") || !entry["coordinate_space"].is_object()) {
        return std::nullopt;
    }

    const auto& space = entry["coordinate_space"];
    const std::string origin = space.value("origin", std::string{});
    if (origin != "bottom_center") {
        return std::nullopt;
    }

    const std::string space_kind = space.value("kind", std::string{});
    const float saved_scale = sanitize_scale(space.value("scale_at_save", 1.0f));
    const float current_scale = sanitize_scale(info.scale_factor);

    CanvasMetrics canonical_canvas = canvas_metrics_for(info);
    CanvasMetrics saved_canvas = metrics_from_json(space);

    SDL_Point render_anchor = scaled_anchor_point(canonical_canvas, current_scale);
    std::optional<AssetInfo::NamedArea::RenderFrame> frame;

    if (space_kind == "render_space") {
        AssetInfo::NamedArea::RenderFrame rf;
        rf.width = saved_canvas.width;
        rf.height = saved_canvas.height;
        if (space.contains("pivot") && space["pivot"].is_object()) {
            rf.pivot_x = space["pivot"].value("x", rf.width / 2);
            rf.pivot_y = space["pivot"].value("y", rf.height);
        } else {
            rf.pivot_x = (rf.width > 0) ? rf.width / 2 : 0;
            rf.pivot_y = rf.height;
        }
        rf.pixel_scale = saved_scale;

        if (rf.is_valid()) {
            frame = rf;

            if (canonical_canvas.width <= 0) {
                canonical_canvas.width = unscale_dimension(rf.width, rf.pixel_scale);
            }
            if (canonical_canvas.height <= 0) {
                canonical_canvas.height = unscale_dimension(rf.height, rf.pixel_scale);
            }

            const int scaled_w = compute_scaled_dimension(canonical_canvas.width, current_scale);
            const int scaled_h = compute_scaled_dimension(canonical_canvas.height, current_scale);
            const double ratio_x = (rf.width > 0) ? static_cast<double>(rf.pivot_x) / static_cast<double>(rf.width) : 0.5;
            const double ratio_y = (rf.height > 0) ? static_cast<double>(rf.pivot_y) / static_cast<double>(rf.height) : 1.0;
            render_anchor.x = static_cast<int>(std::llround(ratio_x * static_cast<double>(scaled_w)));
            render_anchor.y = static_cast<int>(std::llround(ratio_y * static_cast<double>(scaled_h)));
        }
    } else if (space_kind == "canonical") {
        if (canonical_canvas.width <= 0) {
            canonical_canvas.width = saved_canvas.width;
        }
        if (canonical_canvas.height <= 0) {
            canonical_canvas.height = saved_canvas.height;
        }
        render_anchor = scaled_anchor_point(canonical_canvas, current_scale);
    } else {
        return std::nullopt;
    }

    std::vector<Area::Point> points = decode_canonical_points(entry["points"], render_anchor, current_scale);
    if (points.size() < 3) {
        return std::nullopt;
    }

    NamedArea named;
    named.name = name;
    named.type = entry.value("type", std::string{});
    named.kind = entry.value("kind", named.type);
    if (named.kind.empty()) {
        named.kind = named.type;
    }

    try {
        if (entry.contains("attachment_subtype") && entry["attachment_subtype"].is_string()) {
            named.attachment_subtype = entry["attachment_subtype"].get<std::string>();
        }
        if (entry.contains("is_on_top") && entry["is_on_top"].is_boolean()) {
            named.attachment_is_on_top = entry["is_on_top"].get<bool>();
        } else if (entry.contains("placed_on_top_parent") && entry["placed_on_top_parent"].is_boolean()) {

            named.attachment_is_on_top = entry["placed_on_top_parent"].get<bool>();
        }
        if (entry.contains("child_candidates") && entry["child_candidates"].is_array()) {
            named.attachment_child_candidates = entry["child_candidates"];
        }
    } catch (...) {

    }
    const int resolution = vibble::grid::clamp_resolution(entry.value("resolution", 2));
    named.area = std::make_unique<Area>(name, points, resolution);
    named.area->set_resolution(resolution);
    const std::string& applied_type = !named.type.empty() ? named.type : named.kind;
    if (!applied_type.empty()) {
        named.area->set_type(applied_type);
    }
    named.render_frame = frame;
    return named;
}
namespace {

AssetInfo::ManifestStoreProvider& manifest_store_provider_slot() {
    static AssetInfo::ManifestStoreProvider provider;
    return provider;
}

}

AssetInfo::AssetInfo(const std::string &asset_folder_name)
    : AssetInfo(asset_folder_name, nlohmann::json::object()) {}

AssetInfo::AssetInfo(const std::string& asset_folder_name, const nlohmann::json& metadata)
: is_shaded(false)
, is_light_source(false) {
        nlohmann::json data = metadata.is_object() ? metadata : nlohmann::json::object();

        std::string resolved_name = data.value("asset_name", asset_folder_name);
        if (resolved_name.empty()) {
                resolved_name = asset_folder_name;
        }
        name = resolved_name;

        const std::string default_dir = assets_root_for(resolved_name).generic_string();
        dir_path_ = derive_asset_directory(data, default_dir);
        if (dir_path_.empty()) {
                dir_path_ = default_dir;
        }
        dir_path_ = prefer_assets_directory(dir_path_, resolved_name);
        info_json_path_.clear();

        initialize_from_json(data);

        if (!info_json_.contains("asset_name") || !info_json_["asset_name"].is_string() || info_json_["asset_name"].get<std::string>().empty()) {
                info_json_["asset_name"] = name;
        }
}

std::shared_ptr<AssetInfo> AssetInfo::from_manifest_entry(const std::string& asset_folder_name,
                                                         const nlohmann::json& metadata) {
    return std::make_shared<AssetInfo>(asset_folder_name, metadata);
}

void AssetInfo::set_manifest_store_provider(ManifestStoreProvider provider) {
    manifest_store_provider_slot() = std::move(provider);
}

AssetInfo::~AssetInfo() {
	clear_light_textures();
	for (auto &[key, anim] : animations) {
                anim.clear_texture_cache();
	}
	animations.clear();
}

void AssetInfo::clear_light_textures() {
	destroy_light_textures(light_sources);
}

void AssetInfo::load_base_properties(const nlohmann::json &data) {
        type = asset_types::canonicalize(data.value("asset_type", std::string{asset_types::object}));
        if (type == asset_types::player) {
                std::cout << "[AssetInfo] Player asset '" << name << "' loaded\n\n";
        }
        start_animation = data.value("start", std::string{"default"});
        z_threshold = data.value("z_threshold", 0);
        passable = has_tag("passable");
        try {
                if (data.contains("tillable")) {
                        tillable = data.at("tillable").get<bool>();
                } else if (data.contains("tileable")) {
                        tillable = data.at("tileable").get<bool>();
                } else if (info_json_.contains("tillable")) {
                        tillable = info_json_.value("tillable", false);
                } else if (info_json_.contains("tileable")) {
                        tillable = info_json_.value("tileable", false);
                } else {
                        tillable = false;
                }
        } catch (...) {

                if (info_json_.contains("tillable")) {
                        tillable = info_json_.value("tillable", false);
                } else {
                        tillable = info_json_.value("tileable", false);
                }
        }
        is_shaded = data.value("has_shading", false);
        min_same_type_distance = data.value("min_same_type_distance", 0);
        min_distance_all = data.value("min_distance_all", 0);
        flipable = data.value("can_invert", false);
        apply_distance_scaling = data.value("apply_distance_scaling", true);
        apply_vertical_scaling = data.value("apply_vertical_scaling", true);
        info_json_["tillable"] = tillable;
        NeighborSearchRadius = std::clamp( data.value("neighbor_search_distance", NeighborSearchRadius), 20, 1000);
        info_json_["neighbor_search_distance"] = NeighborSearchRadius;
        if (info_json_.is_object()) {
                info_json_.erase("apply_parallax");
        }
}

bool AssetInfo::has_tag(const std::string &tag) const {
    return tag_lookup_.find(tag) != tag_lookup_.end();
}

void AssetInfo::generate_lights(SDL_Renderer* renderer) {
	clear_light_textures();
	try {
		LightingLoader::load(*this, info_json_);
	} catch (...) {
		std::cerr << "[AssetInfo] Failed to regenerate lighting info for '" << name << "'\n";
		return;
	}

	if (!renderer || light_sources.empty()) {
		return;
	}

	std::vector<std::string> signatures;
	signatures.reserve(light_sources.size());
	for (const auto& light : light_sources) {
		signatures.push_back(light_signature(light));
	}

	const fs::path cache_dir = fs::path("cache") / name / "lights";

	auto load_from_cache = [&]() -> bool {
		return try_load_cached_lights(cache_dir, renderer, light_sources, signatures);
};

    bool loaded = load_from_cache();
    if (!loaded && regenerate_lights_via_python(name)) {
        loaded = load_from_cache();
    }

    if (loaded) {
        return;
    }

    clear_light_textures();
    std::cerr << "[AssetInfo] Missing light cache for '" << name
              << "' after python regeneration; run tools/light_tool.py manually.\n";
}

bool AssetInfo::rebuild_light_texture(SDL_Renderer* renderer, std::size_t light_index) {
    if (!renderer) {
        return false;
    }
    if (light_index >= light_sources.size()) {
        return false;
    }

    LightSource& light = light_sources[light_index];

    if (light.texture) {
        SDL_DestroyTexture(light.texture);
        light.texture = nullptr;
        light.cached_w = 0;
        light.cached_h = 0;
    }

    const std::filesystem::path png_path = std::filesystem::path("cache") / name / "lights" / ("light_" + std::to_string(light_index) + ".png");
    SDL_Surface* surf = CacheManager::load_surface(png_path.generic_string());
    if (!surf) {
        return false;
    }

    SDL_Texture* tex = CacheManager::surface_to_texture(renderer, surf);
    const int w = surf->w;
    const int h = surf->h;
    SDL_FreeSurface(surf);

    if (!tex) {
        return false;
    }

    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    light.texture = tex;
    light.cached_w = w;
    light.cached_h = h;
    return true;
}

bool AssetInfo::ensure_light_textures(SDL_Renderer* renderer) {
    if (!renderer) {
        return false;
    }

    bool missing = false;
    for (const auto& light : light_sources) {
        if (!light.texture || light.cached_w <= 0 || light.cached_h <= 0) {
            missing = true;
            break;
        }
    }

    if (!missing) {
        return true;
    }

    generate_lights(renderer);

    bool all_loaded = true;
    for (std::size_t i = 0; i < light_sources.size(); ++i) {
        if (!rebuild_light_texture(renderer, i)) {
            all_loaded = false;
        }
    }
    return all_loaded;
}

bool AssetInfo::commit_manifest() {
        nlohmann::json payload = info_json_;
        if (!payload.contains("asset_name") || !payload["asset_name"].is_string() || payload["asset_name"].get<std::string>().empty()) {
                payload["asset_name"] = name;
        }

        auto& provider = manifest_store_provider_slot();
        if (!provider) {
                std::cerr << "[AssetInfo] Manifest store provider unavailable; cannot commit '" << name << "'\n";
                return false;
        }

        auto* store = provider();
        if (!store) {
                std::cerr << "[AssetInfo] Manifest store not provided; cannot commit '" << name << "'\n";
                return false;
        }

        auto session = store->begin_asset_edit(name, true);
        if (!session) {
                std::cerr << "[AssetInfo] Failed to open manifest session for '" << name << "'\n";
                return false;
        }

        session.data() = payload;
        if (!session.commit()) {
                std::cerr << "[AssetInfo] Failed to commit manifest payload for '" << name << "'\n";
                session.cancel();
                return false;
        }

        store->flush();
        info_json_ = std::move(payload);
        return true;
}

void AssetInfo::set_asset_type(const std::string &t) {
        std::string canonical = asset_types::canonicalize(t);
        type = canonical;
        info_json_["asset_type"] = canonical;
}

void AssetInfo::set_z_threshold(int z) {
	z_threshold = z;
	info_json_["z_threshold"] = z;
}

void AssetInfo::set_min_same_type_distance(int d) {
	min_same_type_distance = d;
	info_json_["min_same_type_distance"] = d;
}

void AssetInfo::set_min_distance_all(int d) {
        min_distance_all = d;
        info_json_["min_distance_all"] = d;
}

void AssetInfo::set_neighbor_search_radius(int radius) {
        NeighborSearchRadius = std::clamp(radius, 20, 1000);
        info_json_["neighbor_search_distance"] = NeighborSearchRadius;
}

void AssetInfo::set_flipable(bool v) {
        flipable = v;
        info_json_["can_invert"] = v;
}

void AssetInfo::set_scale_factor(float factor) {
	if (factor < 0.f)
	factor = 0.f;
	scale_factor = factor;
	if (!info_json_.contains("size_settings") ||
	!info_json_["size_settings"].is_object()) {
		info_json_["size_settings"] = nlohmann::json::object();
	}
	info_json_["size_settings"]["scale_percentage"] = factor * 100.0f;
}

void AssetInfo::set_scale_percentage(float percent) {
        scale_factor = percent / 100.0f;
        if (!info_json_.contains("size_settings") ||
        !info_json_["size_settings"].is_object()) {
                info_json_["size_settings"] = nlohmann::json::object();
        }
        info_json_["size_settings"]["scale_percentage"] = percent;
}

void AssetInfo::set_scale_filter(bool smooth) {
        smooth_scaling = smooth;
        if (!info_json_.contains("size_settings") ||
        !info_json_["size_settings"].is_object()) {
                info_json_["size_settings"] = nlohmann::json::object();
        }
        info_json_["size_settings"]["scale_filter"] = smooth ? "linear" : "nearest";
}

void AssetInfo::set_apply_distance_scaling(bool v) {
        apply_distance_scaling = v;
        if (!info_json_.is_object()) {
                info_json_ = nlohmann::json::object();
        }
        info_json_["apply_distance_scaling"] = v;
}

void AssetInfo::set_apply_vertical_scaling(bool v) {
        apply_vertical_scaling = v;
        if (!info_json_.is_object()) {
                info_json_ = nlohmann::json::object();
        }
        info_json_["apply_vertical_scaling"] = v;
}

void AssetInfo::set_tags(const std::vector<std::string> &t) {
        tags = t;
        rebuild_tag_cache();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto &s : tags)
        arr.push_back(s);
        info_json_["tags"] = std::move(arr);
        passable = has_tag("passable");
}

void AssetInfo::add_tag(const std::string &tag) {
        if (!has_tag(tag)) {
                tags.push_back(tag);
        }
        set_tags(tags);
}

void AssetInfo::remove_tag(const std::string &tag) {
        tags.erase(std::remove(tags.begin(), tags.end(), tag), tags.end());
        set_tags(tags);
}

void AssetInfo::set_anti_tags(const std::vector<std::string> &t) {
        anti_tags = t;
        rebuild_anti_tag_cache();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto &s : anti_tags)
                arr.push_back(s);
        info_json_["anti_tags"] = std::move(arr);
}

void AssetInfo::add_anti_tag(const std::string &tag) {
        if (anti_tag_lookup_.find(tag) == anti_tag_lookup_.end()) {
                anti_tags.push_back(tag);
        }
        set_anti_tags(anti_tags);
}

void AssetInfo::remove_anti_tag(const std::string &tag) {
        anti_tags.erase(std::remove(anti_tags.begin(), anti_tags.end(), tag), anti_tags.end());
        set_anti_tags(anti_tags);
}

void AssetInfo::set_animation_children(const std::vector<std::string>& children) {
        animation_children.clear();
        std::unordered_set<std::string> seen;
        for (const auto& entry : children) {
                if (entry.empty()) {
                        continue;
                }
                if (seen.insert(entry).second) {
                        animation_children.push_back(entry);
                }
        }
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& name : animation_children) {
                arr.push_back(name);
        }
        info_json_["animation_children"] = std::move(arr);

    for (auto& [anim_id, anim] : animations) {
        std::vector<std::string> merged;
        merged.reserve(animation_children.size() + anim.child_assets().size());
        std::unordered_set<std::string> anim_seen;
        for (const auto& name : animation_children) {
            if (name.empty()) continue;
            if (anim_seen.insert(name).second) {
                merged.push_back(name);
            }
        }
        for (const auto& name : anim.child_assets()) {
            if (name.empty()) continue;
            if (anim_seen.insert(name).second) {
                merged.push_back(name);
            }
        }
        anim.child_assets() = std::move(merged);
        anim.rebuild_child_timelines_from_frames();
    }
}

void AssetInfo::append_animation_child(const std::string& child) {
    if (child.empty()) {
        return;
    }
    animation_children.push_back(child);
    if (!info_json_.contains("animation_children") || !info_json_["animation_children"].is_array()) {
        info_json_["animation_children"] = nlohmann::json::array();
    }
    info_json_["animation_children"].push_back(child);
}

void AssetInfo::remove_animation_child_at(std::size_t index) {
    if (index >= animation_children.size()) {
        return;
    }
    animation_children.erase(animation_children.begin() + static_cast<long>(index));
    if (!info_json_.contains("animation_children") || !info_json_["animation_children"].is_array()) {
        info_json_["animation_children"] = animation_children;
        return;
    }
    auto& arr = info_json_["animation_children"];
    if (!arr.is_array()) {
        info_json_["animation_children"] = animation_children;
        return;
    }
    if (index < arr.size()) {
        arr.erase(arr.begin() + static_cast<nlohmann::json::difference_type>(index));
    } else {
        info_json_["animation_children"] = animation_children;
    }
}

    void AssetInfo::set_async_children(const std::vector<AsyncChildDefinition>& children) {
        async_children.clear();
        std::unordered_set<std::string> seen;
        async_children.reserve(children.size());
        for (const auto& entry : children) {
            if (!entry.valid()) {
                continue;
            }
            if (!seen.insert(entry.name).second) {
                continue;
            }
            async_children.push_back(entry);
        }

        nlohmann::json arr = nlohmann::json::array();
        for (const auto& child : async_children) {
            nlohmann::json obj = nlohmann::json::object();
            obj["name"] = child.name;
            obj["asset"] = child.asset;
            if (!child.animation.empty()) {
                obj["animation"] = child.animation;
            }
            obj["frames"] = encode_async_child_frames(child.frames);
            arr.push_back(std::move(obj));
        }
        info_json_["async_children"] = std::move(arr);
    }

void AssetInfo::rebuild_tag_cache() {
        tag_lookup_.clear();
        tag_lookup_.reserve(tags.size());
        for (const auto& value : tags) {
                tag_lookup_.insert(value);
        }
}

void AssetInfo::rebuild_anti_tag_cache() {
    anti_tag_lookup_.clear();
    anti_tag_lookup_.reserve(anti_tags.size());
    for (const auto& value : anti_tags) {
        anti_tag_lookup_.insert(value);
    }
}

#if defined(ASSET_INFO_ENABLE_TEST_ACCESS)
void AssetInfoTestAccess::initialize_info_json(AssetInfo& info, nlohmann::json data) {
    info.info_json_ = std::move(data);
}

void AssetInfoTestAccess::rebuild_tag_cache(AssetInfo& info) {
    info.rebuild_tag_cache();
}

void AssetInfoTestAccess::rebuild_anti_tag_cache(AssetInfo& info) {
    info.rebuild_anti_tag_cache();
}
#endif

void AssetInfo::set_passable(bool v) {
        passable = v;
        if (v)
        add_tag("passable");
        else
        remove_tag("passable");
}

void AssetInfo::set_tillable(bool v) {
        tillable = v;

        info_json_["tillable"] = v;
        info_json_["tileable"] = v;
}

void AssetInfo::set_shadow_mask_settings(const ShadowMaskSettings& settings) {
        shadow_mask_settings = SanitizeShadowMaskSettings(settings);
        if (!info_json_.is_object()) {
                info_json_ = nlohmann::json::object();
        }
        nlohmann::json serialized = nlohmann::json::object();
        serialized["expansion_ratio"]  = shadow_mask_settings.expansion_ratio;
        serialized["blur_scale"]       = shadow_mask_settings.blur_scale;
        serialized["falloff_start"]    = shadow_mask_settings.falloff_start;
        serialized["falloff_exponent"] = shadow_mask_settings.falloff_exponent;
        serialized["alpha_multiplier"] = shadow_mask_settings.alpha_multiplier;
        info_json_["shadow_mask_settings"] = std::move(serialized);
}

void AssetInfo::set_shading_enabled(bool enabled) {
        is_shaded = enabled;
        is_light_source = enabled || !light_sources.empty();
        if (!info_json_.is_object()) {
                info_json_ = nlohmann::json::object();
        }
        info_json_["has_shading"] = enabled;
}

namespace {
constexpr float kShadingParallaxMin = 0.0f;
constexpr float kShadingParallaxMax = 4.0f;
constexpr float kShadingBrightnessMin = 0.0f;
constexpr float kShadingBrightnessMax = 4.0f;
constexpr float kShadingOpacityMin = 0.0f;
constexpr float kShadingOpacityMax = 4.0f;

float sanitize_shading_ratio(float value, float lo, float hi, float fallback) {
    if (!std::isfinite(value)) {
        return fallback;
    }
    return std::clamp(value, lo, hi);
}
}

void AssetInfo::set_shading_parallax_amount(float amount) {
        float sanitized = sanitize_shading_ratio(amount, kShadingParallaxMin, kShadingParallaxMax, shading_parallax_amount);
        shading_parallax_amount = sanitized;
        if (!info_json_.is_object()) {
                info_json_ = nlohmann::json::object();
        }
        info_json_["shading_parallax_amount"] = sanitized;
}

void AssetInfo::set_shading_screen_brightness_multiplier(float multiplier) {
        float sanitized = sanitize_shading_ratio( multiplier, kShadingBrightnessMin, kShadingBrightnessMax, shading_screen_brightness_multiplier);
        shading_screen_brightness_multiplier = sanitized;
        if (!info_json_.is_object()) {
                info_json_ = nlohmann::json::object();
        }
        info_json_["shading_screen_brightness_multiplier"] = sanitized;
}

void AssetInfo::set_shading_opacity_multiplier(float multiplier) {
        float sanitized = sanitize_shading_ratio( multiplier, kShadingOpacityMin, kShadingOpacityMax, shading_opacity_multiplier);
        shading_opacity_multiplier = sanitized;
        if (!info_json_.is_object()) {
                info_json_ = nlohmann::json::object();
        }
        info_json_["shading_opacity_multiplier"] = sanitized;
}

Area* AssetInfo::find_area(const std::string& name) {
	for (auto& na : areas) {
		if (na.name == name) return na.area.get();
	}
	return nullptr;
}
void AssetInfo::upsert_area_from_editor(const Area& area,
                                        std::optional<NamedArea::RenderFrame> frame) {
    if (area.get_name().empty()) {
        return;
    }

    if (!info_json_.contains("areas") || !info_json_["areas"].is_array()) {
        info_json_["areas"] = nlohmann::json::array();
    }

    nlohmann::json* existing_entry = nullptr;
    std::string existing_type;
    std::string existing_kind;
    for (auto& entry : info_json_["areas"]) {
        if (!entry.is_object()) continue;
        if (entry.value("name", std::string{}) == area.get_name()) {
            existing_entry = &entry;
            existing_type = entry.value("type", std::string{});
            existing_kind = entry.value("kind", std::string{});
            break;
        }
    }

    const std::string final_type = !area.get_type().empty() ? area.get_type() : existing_type;
    std::string final_kind = existing_kind;
    if (final_kind.empty()) final_kind = final_type;

    bool updated = false;
    for (auto& na : areas) {
        if (na.name == area.get_name()) {
            na.area = std::make_unique<Area>(area);
            if (!final_type.empty()) na.type = final_type;
            if (!final_kind.empty()) na.kind = final_kind;
            na.render_frame = frame;
            updated = true;
            break;
        }
    }
    if (!updated) {
        NamedArea na;
        na.name = area.get_name();
        na.type = final_type;
        na.kind = final_kind;
        na.area = std::make_unique<Area>(area);
        na.render_frame = frame;
        areas.push_back(std::move(na));
    }

    nlohmann::json entry =
        AreaCodec::encode_entry(*this, area, final_type, final_kind, frame);

    if (existing_entry && existing_entry->is_object()) {
        static const char* kAttachmentKeys[] = {
            "attachment_subtype", "is_on_top", "child_candidates", "placed_on_top_parent", "z_offset"
};
        for (const char* key : kAttachmentKeys) {
            auto it = existing_entry->find(key);
            if (it != existing_entry->end()) {
                entry[key] = *it;
            }
        }
    }

    if (existing_entry) {
        *existing_entry = std::move(entry);
    } else {
        info_json_["areas"].push_back(std::move(entry));
    }
}

std::string AssetInfo::pick_next_animation(const std::string& mapping_id) const {
	auto it = mappings.find(mapping_id);
	if (it == mappings.end()) return {};
	static std::mt19937 rng{std::random_device{}()};
	for (const auto& entry : it->second) {
		if (!entry.condition.empty() && entry.condition != "true") continue;
		float total = 0.0f;
		for (const auto& opt : entry.options) {
			total += opt.percent;
		}
		if (total <= 0.0f) continue;
		std::uniform_real_distribution<float> dist(0.0f, total);
		float r = dist(rng);
		for (const auto& opt : entry.options) {
			if ((r -= opt.percent) <= 0.0f) {
					return opt.animation;
			}
		}
	}
	return {};
}

void AssetInfo::load_areas(const nlohmann::json& data) {
        areas.clear();
        if (!data.contains("areas") || !data["areas"].is_array()) {
                return;
        }

        for (const auto& entry : data["areas"]) {
                auto decoded = AreaCodec::decode_entry(*this, entry);
                if (!decoded) {
                        continue;
                }
                areas.push_back(std::move(*decoded));
        }
}

void AssetInfo::load_children(const nlohmann::json& data) {
    ChildLoader::load_children(*this, data, dir_path_);
}

void AssetInfo::load_animations(const nlohmann::json& data) {
    const nlohmann::json* payloads = locate_animation_payloads(data);

    nlohmann::json new_anim = nlohmann::json::object();
    if (payloads && payloads->is_object()) {
        for (auto it = payloads->begin(); it != payloads->end(); ++it) {
            if (!it.value().is_object()) {
                continue;
            }
            const auto& anim_json = it.value();
            nlohmann::json converted = anim_json;
            if (!anim_json.contains("source")) {
                converted["source"] = {
                    {"kind", "folder"},
                    {"path", anim_json.value("frames_path", it.key())}
};
                converted["locked"] = anim_json.value("lock_until_done", false);
                converted.erase("frames_path");
                converted.erase("lock_until_done");
                converted.erase("speed");
                converted.erase("speed_factor");
                converted.erase("fps");
            }
            new_anim[it.key()] = std::move(converted);
        }
    }

    anims_json_ = std::move(new_anim);
    if (!info_json_.is_object()) {
        info_json_ = nlohmann::json::object();
    }
    info_json_["animations"] = anims_json_;
}

void AssetInfo::initialize_from_json(const nlohmann::json& source) {
        nlohmann::json data = source.is_object() ? source : nlohmann::json::object();

        info_json_ = data;

        tags = parse_string_array(data.value("tags", nlohmann::json::array()));
        anti_tags = parse_string_array(data.value("anti_tags", nlohmann::json::array()));
        rebuild_tag_cache();
        rebuild_anti_tag_cache();

        animation_children = parse_string_array(data.value("animation_children", nlohmann::json::array()));
        if (animation_children.empty()) {
                const nlohmann::json* anim_payloads = nullptr;
                if (data.contains("animations") && data["animations"].is_object()) {
                        anim_payloads = &data["animations"];
                        if (data["animations"].contains("animations") && data["animations"]["animations"].is_object()) {
                                anim_payloads = &data["animations"]["animations"];
                        }
                }
                if (anim_payloads) {
                        std::unordered_set<std::string> seen;
                        for (const auto& item : anim_payloads->items()) {
                                if (!item.value().is_object()) continue;
                                auto it_children = item.value().find("children");
                                if (it_children != item.value().end() && it_children->is_array()) {
                                    for (const auto& entry : *it_children) {
                                        if (!entry.is_string()) continue;
                                        std::string name = entry.get<std::string>();
                                        if (name.empty() || !seen.insert(name).second) continue;
                                        animation_children.push_back(std::move(name));
                                    }
                                }

                                auto it_timelines = item.value().find("child_timelines");
                                if (it_timelines != item.value().end() && it_timelines->is_array()) {
                                    for (const auto& entry : *it_timelines) {
                                        if (!entry.is_object()) continue;
                                        std::string name;
                                        if (entry.contains("asset") && entry["asset"].is_string()) {
                                            name = entry["asset"].get<std::string>();
                                        }
                                        if (name.empty()) {
                                            continue;
                                        }
                                        if (!seen.insert(name).second) continue;
                                        animation_children.push_back(std::move(name));
                                    }
                                }
                        }
                }
        }

        if (!info_json_.contains("tags") || !info_json_["tags"].is_array()) {
                info_json_["tags"] = nlohmann::json::array();
        }
        if (!info_json_.contains("anti_tags") || !info_json_["anti_tags"].is_array()) {
                info_json_["anti_tags"] = nlohmann::json::array();
        }
        nlohmann::json animation_children_json = nlohmann::json::array();
        for (const auto& name : animation_children) {
                if (!name.empty()) {
                        animation_children_json.push_back(name);
                }
        }
        info_json_["animation_children"] = std::move(animation_children_json);

        async_children = parse_async_children(data);
        set_async_children(async_children);

        load_animations(data);

        mappings.clear();
        if (data.contains("mappings") && data["mappings"].is_object()) {
                for (auto it = data["mappings"].begin(); it != data["mappings"].end(); ++it) {
                        const std::string id = it.key();
                        Mapping map;
                        if (it.value().is_array()) {
                                for (const auto& entry_json : it.value()) {
                                        if (!entry_json.is_object()) {
                                                continue;
                                        }
                                        MappingEntry me;
                                        me.condition = entry_json.value("condition", "");
                                        if (entry_json.contains("map_to") && entry_json["map_to"].contains("options")) {
                                                for (const auto& opt_json : entry_json["map_to"]["options"]) {
                                                        if (!opt_json.is_object()) {
                                                                continue;
                                                        }
                                                        MappingOption opt{opt_json.value("animation", ""), opt_json.value("percent", 100.0f)};
                                                        me.options.push_back(opt);
                                                }
                                        }
                                        map.push_back(std::move(me));
                                }
                        }
                        mappings[id] = std::move(map);
                }
                info_json_["mappings"] = data["mappings"];
        }

        smooth_scaling = true;
        if (has_tag("pixel_art") || has_tag("preserve_pixels")) {
                smooth_scaling = false;
        }

        load_base_properties(data);
        LightingLoader::load(*this, data);

        ShadowMaskSettings parsed_settings{};
        if (data.contains("shadow_mask_settings") && data["shadow_mask_settings"].is_object()) {
                const auto& json_settings = data["shadow_mask_settings"];
                parsed_settings.expansion_ratio  = json_settings.value("expansion_ratio", parsed_settings.expansion_ratio);
                parsed_settings.blur_scale       = json_settings.value("blur_scale", parsed_settings.blur_scale);
                parsed_settings.falloff_start    = json_settings.value("falloff_start", parsed_settings.falloff_start);
                parsed_settings.falloff_exponent = json_settings.value("falloff_exponent", parsed_settings.falloff_exponent);
                parsed_settings.alpha_multiplier = json_settings.value("alpha_multiplier", parsed_settings.alpha_multiplier);
        }
        set_shadow_mask_settings(parsed_settings);

        set_shading_parallax_amount(read_float_field(data, "shading_parallax_amount", shading_parallax_amount));
        set_shading_screen_brightness_multiplier( read_float_field(data, "shading_screen_brightness_multiplier", shading_screen_brightness_multiplier));
        set_shading_opacity_multiplier( read_float_field(data, "shading_opacity_multiplier", shading_opacity_multiplier));

        const auto &ss = data.value("size_settings", nlohmann::json::object());
        scale_factor = ss.value("scale_percentage", 100.0f) / 100.0f;
        if (ss.contains("scale_filter")) {
                std::string filter = ss.value("scale_filter", std::string{});
                for (char& ch : filter) {
                        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                }
                if (!filter.empty()) {
                        smooth_scaling = !(filter == "nearest" || filter == "point" || filter == "none");
                }
        }

        try {
                if (data.contains("canvas_width") && data["canvas_width"].is_number_integer()) {
                        original_canvas_width = std::max(0, data["canvas_width"].get<int>());
                }
                if (data.contains("canvas_height") && data["canvas_height"].is_number_integer()) {
                        original_canvas_height = std::max(0, data["canvas_height"].get<int>());
                }
        } catch (...) {

        }

        load_children(data);

        try {
                if (data.contains("custom_controller_key") && data["custom_controller_key"].is_string()) {
                        custom_controller_key = data["custom_controller_key"].get<std::string>();
                } else {
                        custom_controller_key.clear();
                }
        } catch (...) {
                custom_controller_key.clear();
        }
}

void AssetInfo::set_children(const std::vector<ChildInfo>& new_children) {

    asset_children = new_children;

    nlohmann::json groups = nlohmann::json::array();
    for (const auto& c : new_children) {
        nlohmann::json entry = nlohmann::json::object();
        if (c.spawn_group.is_object()) {
            entry = c.spawn_group;
        }

        if (!c.area_name.empty()) {
            entry["linked_area"] = c.area_name;
            entry["link_to_area"] = true;
        }

        entry["z_offset"] = c.z_offset;
        entry["placed_on_top_parent"] = c.placed_on_top_parent;

        if (!entry.contains("candidates") || !entry["candidates"].is_array()) {
            entry["candidates"] = nlohmann::json::array();
        }

        groups.push_back(std::move(entry));
    }

    set_spawn_groups(groups);
}

void AssetInfo::set_spawn_groups_payload(const nlohmann::json& groups) {
    if (!info_json_.is_object()) {
        info_json_ = nlohmann::json::object();
    }

    if (groups.is_array()) {
        info_json_["spawn_groups"] = groups;
    } else {
        info_json_.erase("spawn_groups");
    }
}

nlohmann::json AssetInfo::spawn_groups_payload() const {
    if (info_json_.is_object()) {
        auto it = info_json_.find("spawn_groups");
        if (it != info_json_.end() && it->is_array()) {
            return *it;
        }
    }
    return nlohmann::json::array();
}

void AssetInfo::set_spawn_groups(const nlohmann::json& groups) {
    nlohmann::json sanitized = nlohmann::json::array();
    if (groups.is_array()) {
        sanitized = groups;
    }

    info_json_["spawn_groups"] = std::move(sanitized);
}

void AssetInfo::set_lighting(const std::vector<LightSource>& lights) {
    light_sources = lights;
    is_light_source = !lights.empty();

    nlohmann::json arr = nlohmann::json::array();

    for (const auto& l : lights) {
        nlohmann::json j;
        j["has_light_source"] = true;
        j["light_intensity"] = l.intensity;
        j["radius"] = l.radius;
        j["falloff"] = l.fall_off;
        j["flicker_speed"] = l.flicker_speed;
        j["flicker_smoothness"] = l.flicker_smoothness;

        j["flicker"] = l.flicker_speed;
        j["flare"] = l.flare;
        j["offset_x"] = l.offset_x;
        j["offset_y"] = l.offset_y;
        j["light_color"] = { l.color.r, l.color.g, l.color.b };
        j["in_front"] = l.in_front;
        j["behind"] = l.behind;
        j["render_to_dark_mask"] = l.render_to_dark_mask;
        j["render_front_and_back_to_asset_alpha_mask"] =
            l.render_front_and_back_to_asset_alpha_mask;
        arr.push_back(std::move(j));
    }
    info_json_["lighting_info"] = std::move(arr);
}

bool AssetInfo::remove_area(const std::string& name) {
    bool removed = false;

    areas.erase(std::remove_if(areas.begin(), areas.end(), [&](const NamedArea& na){ return na.name == name; }), areas.end());

    try {
        if (info_json_.contains("areas") && info_json_["areas"].is_array()) {
            nlohmann::json new_arr = nlohmann::json::array();
            for (const auto& entry : info_json_["areas"]) {
                if (entry.is_object() && entry.value("name", std::string{}) == name) {
                    removed = true;
                    continue;
                }
                new_arr.push_back(entry);
            }
            info_json_["areas"] = std::move(new_arr);
        }
    } catch (...) {

    }
    return removed;
}

bool AssetInfo::rename_area(const std::string& old_name, const std::string& new_name) {
    if (old_name.empty() || new_name.empty()) {
        return false;
    }
    if (old_name == new_name) {
        return true;
    }

    auto conflict = std::find_if(areas.begin(), areas.end(), [&](const NamedArea& na) {
        return na.name == new_name;
    });
    if (conflict != areas.end()) {
        return false;
    }

    bool renamed = false;
    for (auto& na : areas) {
        if (na.name == old_name) {
            na.name = new_name;
            if (na.area) {
                na.area->set_name(new_name);
            }
            renamed = true;
        }
    }
    if (!renamed) {
        return false;
    }

    try {
        if (info_json_.contains("areas") && info_json_["areas"].is_array()) {
            for (auto& entry : info_json_["areas"]) {
                if (entry.is_object() && entry.value("name", std::string{}) == old_name) {
                    entry["name"] = new_name;
                }
            }
        }
    } catch (...) {

    }

    return true;
}

std::vector<std::string> AssetInfo::animation_names() const {
	std::vector<std::string> names;
	try {
		if (info_json_.contains("animations") && info_json_["animations"].is_object()) {
			for (auto it = info_json_["animations"].begin(); it != info_json_["animations"].end(); ++it) {
				names.push_back(it.key());
			}
		}
	} catch (...) {

	}
	std::sort(names.begin(), names.end());
	return names;
}

nlohmann::json AssetInfo::animation_payload(const std::string& name) const {
	try {
		if (info_json_.contains("animations") && info_json_["animations"].is_object()) {
			auto it = info_json_["animations"].find(name);
			if (it != info_json_["animations"].end()) {
				return *it;
			}
		}
	} catch (...) {}
	return nlohmann::json::object();
}

bool AssetInfo::upsert_animation(const std::string& name, const nlohmann::json& payload) {
	if (name.empty()) return false;
	try {
		if (!info_json_.contains("animations") || !info_json_["animations"].is_object()) {
			info_json_["animations"] = nlohmann::json::object();
		}
		info_json_["animations"][name] = payload;

		if (anims_json_.is_null() || !anims_json_.is_object()) anims_json_ = nlohmann::json::object();
		anims_json_[name] = payload;
		return true;
	} catch (...) {
		return false;
	}
}

bool AssetInfo::remove_animation(const std::string& name) {
	bool removed = false;
	try {
		if (info_json_.contains("animations") && info_json_["animations"].is_object()) {
			removed = info_json_["animations"].erase(name) > 0;
		}
		if (anims_json_.is_object()) {
			anims_json_.erase(name);
		}
		if (start_animation == name) {
			start_animation.clear();
			info_json_["start"] = start_animation;
		}
	} catch (...) {
		removed = false;
	}
	return removed;
}

bool AssetInfo::rename_animation(const std::string& old_name, const std::string& new_name) {
	if (old_name.empty() || new_name.empty() || old_name == new_name) return false;
	try {
		nlohmann::json payload;
		bool found = false;
		if (info_json_.contains("animations") && info_json_["animations"].is_object()) {
			auto it = info_json_["animations"].find(old_name);
			if (it != info_json_["animations"].end()) { payload = *it; found = true; }
		}
		if (!found) return false;

		info_json_["animations"][new_name] = payload;
		info_json_["animations"].erase(old_name);
		if (anims_json_.is_null() || !anims_json_.is_object()) anims_json_ = nlohmann::json::object();
		anims_json_[new_name] = payload;
		anims_json_.erase(old_name);
		if (start_animation == old_name) {
			start_animation = new_name;
		 info_json_["start"] = start_animation;
		}
		return true;
	} catch (...) {
		return false;
	}
}

void AssetInfo::set_start_animation_name(const std::string& name) {
        try {
                start_animation = name;
                info_json_["start"] = name;
        } catch (...) {

        }
}

bool AssetInfo::reload_animations_from_disk() {
    auto apply_payload = [this](const nlohmann::json& payload) -> bool {
        if (!payload.is_object()) {
            return false;
        }

        load_animations(payload);

        std::string new_start = start_animation;
        std::string candidate;
        if (extract_start_value(payload, candidate)) {
            new_start = std::move(candidate);
        }
        if (new_start.empty()) {
            new_start = start_animation;
        }
        if (new_start.empty()) {
            new_start = "default";
        }

        start_animation = new_start;
        if (!info_json_.is_object()) {
            info_json_ = nlohmann::json::object();
        }
        info_json_["start"] = start_animation;
        return true;
};

    auto& provider = manifest_store_provider_slot();
    if (!provider) {
        return false;
    }
    devmode::core::ManifestStore* store = provider();
    if (!store) {
        return false;
    }
    auto view = store->get_asset(name);
    if (!view || !view.data) {
        return false;
    }
    return apply_payload(*view.data);
}

bool AssetInfo::update_animation_properties(const std::string& animation_name, const nlohmann::json& properties) {
    if (animation_name.empty() || !properties.is_object()) {
        return false;
    }

    try {

        if (!anims_json_.is_object()) {
            anims_json_ = nlohmann::json::object();
        }

        nlohmann::json updated_animation = properties;
        if (anims_json_.contains(animation_name) && anims_json_[animation_name].is_object()) {

            for (auto& [key, value] : anims_json_[animation_name].items()) {
                if (!updated_animation.contains(key)) {
                    updated_animation[key] = value;
                }
            }
        }

        anims_json_[animation_name] = updated_animation;

        if (!info_json_.is_object()) {
            info_json_ = nlohmann::json::object();
        }
        if (!info_json_.contains("animations") || !info_json_["animations"].is_object()) {
            info_json_["animations"] = nlohmann::json::object();
        }
        info_json_["animations"][animation_name] = updated_animation;

        if (properties.contains("start") && properties["start"].is_boolean() && properties["start"].get<bool>()) {
            start_animation = animation_name;
            info_json_["start"] = start_animation;
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "[AssetInfo] Failed to update animation properties for '" << animation_name << "': " << e.what() << std::endl;
        return false;
    }
}

void AssetInfo::loadAnimations(SDL_Renderer* renderer) {
    if (!anims_json_.is_object()) return;

    SDL_Texture* dummy_base_sprite = nullptr;
    int dummy_w = 0;
    int dummy_h = 0;

    auto parse_source_animation = [](const nlohmann::json& payload) -> std::optional<std::string> {
        if (!payload.contains("source") || !payload["source"].is_object()) {
            return std::nullopt;
        }
        const auto& source = payload["source"];
        try {
            const std::string kind = source.value("kind", std::string{});
            if (kind != "animation") {
                return std::nullopt;
            }
            const std::string name = source.value("name", std::string{});
            if (name.empty()) {
                return std::nullopt;
            }
            return name;
        } catch (...) {
            return std::nullopt;
        }
};

    auto animation_ready = [this](const std::string& name) {
        auto it = animations.find(name);
        if (it == animations.end()) {
            return false;
        }
        const Animation& anim = it->second;
        return anim.number_of_frames > 0 && !anim.frames.empty();
};

    for (auto it = anims_json_.begin(); it != anims_json_.end(); ++it) {
        animations[it.key()];
    }

    std::filesystem::path cache_root = std::filesystem::path("cache") / this->name / "animations";
    auto load_single = [&](const std::string& name, const nlohmann::json& json) {
        Animation& anim = animations[name];
        AnimationLoader::load(anim, name, json, *this, dir_path_, cache_root.string(), scale_factor, renderer, dummy_base_sprite, dummy_w, dummy_h, original_canvas_width, original_canvas_height, false);
};

    std::vector<std::pair<std::string, nlohmann::json>> deferred;

    for (auto it = anims_json_.begin(); it != anims_json_.end(); ++it) {
        const std::string name = it.key();
        const auto& json       = it.value();

        auto source_name = parse_source_animation(json);
        const bool needs_source = source_name.has_value() && *source_name != name;
        if (needs_source && !animation_ready(*source_name)) {
            deferred.emplace_back(name, json);
            continue;
        }

        load_single(name, json);
    }

    std::size_t safety_counter = deferred.size() + 1;
    while (!deferred.empty() && safety_counter-- > 0) {
        bool progress = false;
        for (auto it = deferred.begin(); it != deferred.end();) {
            auto source_name = parse_source_animation(it->second);
            const bool ready = !source_name || source_name->empty() || *source_name == it->first || animation_ready(*source_name);
            if (!ready) {
                ++it;
                continue;
            }

            load_single(it->first, it->second);
            it = deferred.erase(it);
            progress = true;
        }

        if (!progress) {
            break;
        }
    }

    for (const auto& pending : deferred) {
        auto source_name = parse_source_animation(pending.second);
        if (source_name) {
            std::cout << "[AssetInfo] Loading derived animation '" << pending.first
                      << "' without ready source '" << *source_name << "'\n";
        } else {
            std::cout << "[AssetInfo] Loading animation '" << pending.first << "'\n";
        }
        load_single(pending.first, pending.second);
    }
}

