#include "asset_info.hpp"
#include "asset_info_methods/animation_loader.hpp"
#include "asset/asset_types.hpp"
#include "asset_info_methods/child_loader.hpp"
#include "asset_info_methods/lighting_loader.hpp"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <random>
#include <limits>
#include <cmath>
#include <cctype>

namespace {

struct CanvasMetrics {
    int width = 0;
    int height = 0;
};

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
    named.area = std::make_unique<Area>(name, points);
    const std::string& applied_type = !named.type.empty() ? named.type : named.kind;
    if (!applied_type.empty()) {
        named.area->set_type(applied_type);
    }
    named.render_frame = frame;
    return named;
}
AssetInfo::AssetInfo(const std::string &asset_folder_name)
: is_shaded(false)
, is_light_source(false) {
	name = asset_folder_name;
	dir_path_ = "SRC/" + asset_folder_name;
	std::string info_path = dir_path_ + "/info.json";
	info_json_path_ = info_path;
	std::ifstream in(info_path);
	if (!in.is_open()) {
		throw std::runtime_error("Failed to open asset info: " + info_path);
	}
	nlohmann::json data;
	in >> data;
	info_json_ = data;
        tags.clear();
        if (data.contains("tags") && data["tags"].is_array()) {
                for (const auto &tag : data["tags"]) {
                        if (tag.is_string()) {
                                        std::string str = tag.get<std::string>();
                                        if (!str.empty())
                                        tags.push_back(str);
                        }
                }
        }
        anti_tags.clear();
        if (data.contains("anti_tags") && data["anti_tags"].is_array()) {
                for (const auto &tag : data["anti_tags"]) {
                        if (tag.is_string()) {
                                        std::string str = tag.get<std::string>();
                                        if (!str.empty())
                                        anti_tags.push_back(str);
                        }
                }
        }
        rebuild_tag_cache();
        rebuild_anti_tag_cache();
        if (data.contains("animations") && data["animations"].is_object()) {
                nlohmann::json new_anim = nlohmann::json::object();
                for (auto it = data["animations"].begin(); it != data["animations"].end(); ++it) {
                        const std::string trig = it.key();
			const auto &anim_json = it.value();
			nlohmann::json converted = anim_json;
			if (!anim_json.contains("source")) {
					converted["source"] = {
								{"kind", "folder"},
								{"path", anim_json.value("frames_path", trig)}};
					converted["locked"] = anim_json.value("lock_until_done", false);
					converted["speed_factor"] = anim_json.value("speed", 1.0f);
					converted.erase("frames_path");
					converted.erase("lock_until_done");
					converted.erase("speed");
			}
			new_anim[trig] = converted;
		}
		anims_json_ = new_anim;
		info_json_["animations"] = new_anim;
	}
	if (data.contains("mappings") && data["mappings"].is_object()) {
		for (auto it = data["mappings"].begin(); it != data["mappings"].end(); ++it) {
			const std::string id = it.key();
			Mapping map;
			if (it.value().is_array()) {
					for (const auto& entry_json : it.value()) {
								MappingEntry me;
								me.condition = entry_json.value("condition", "");
								if (entry_json.contains("map_to") && entry_json["map_to"].contains("options")) {
													for (const auto& opt_json : entry_json["map_to"]["options"]) {
																					MappingOption opt{opt_json.value("animation", ""), opt_json.value("percent", 100.0f)};
																					me.options.push_back(opt);
													}
								}
								map.push_back(me);
					}
			}
			mappings[id] = map;
		}
                info_json_["mappings"] = data["mappings"];
        }
        smooth_scaling = true;
        if (has_tag("pixel_art") || has_tag("preserve_pixels")) {
                smooth_scaling = false;
        }
        load_base_properties(data);
        LightingLoader::load(*this, data);
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
        load_children(data);
	try {
		if (data.contains("custom_controller_key") && data["custom_controller_key"].is_string()) {
			custom_controller_key = data["custom_controller_key"].get<std::string>();
		}
	} catch (...) {
		custom_controller_key.clear();
	}
}

AssetInfo::~AssetInfo() {
	std::ostringstream oss;
	oss << "[AssetInfo] Destructor for '" << name << "'\r";
	std::cout << std::left << std::setw(60) << oss.str() << std::flush;
	for (auto &[key, anim] : animations) {
                anim.clear_texture_cache();
	}
	animations.clear();
}

void AssetInfo::loadAnimations(SDL_Renderer *renderer) {
        AnimationLoader::load(*this, renderer);

        const bool has_canvas = original_canvas_width > 0 && original_canvas_height > 0;
        if (!has_canvas) {
                areas.clear();
                return;
        }

        load_areas(info_json_);
        AnimationLoader::get_area_textures(*this, renderer);
}

void AssetInfo::load_base_properties(const nlohmann::json &data) {
        type = asset_types::canonicalize(data.value("asset_type", std::string{asset_types::object}));
        if (type == asset_types::player) {
                std::cout << "[AssetInfo] Player asset '" << name << "' loaded\n\n";
        }
	start_animation = data.value("start", std::string{"default"});
        z_threshold = data.value("z_threshold", 0);
        passable = has_tag("passable");
        is_shaded = data.value("has_shading", false);
        min_same_type_distance = data.value("min_same_type_distance", 0);
        min_distance_all = data.value("min_distance_all", 0);
        flipable = data.value("can_invert", false);
        generate_rays = data.value("generate_rays", false);
        info_json_["generate_rays"] = generate_rays;
        ray_strength = std::clamp(data.value("ray_strength", 0), 0, 100);
        info_json_["ray_strength"] = ray_strength;
        NeighborSearchRadius = std::clamp( data.value("neighbor_search_distance", NeighborSearchRadius), 20, 1000);
        info_json_["neighbor_search_distance"] = NeighborSearchRadius;
}

bool AssetInfo::has_tag(const std::string &tag) const {
    return tag_lookup_.find(tag) != tag_lookup_.end();
}

void AssetInfo::generate_lights(SDL_Renderer *renderer) {
	LightingLoader::generate_textures(*this, renderer);
}

bool AssetInfo::update_info_json() const {
	try {
		std::ofstream out(info_json_path_);
		if (!out.is_open())
		return false;
		out << info_json_.dump(4);
		return true;
	} catch (...) {
		return false;
	}
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

void AssetInfo::set_passable(bool v) {
        passable = v;
        if (v)
        add_tag("passable");
        else
        remove_tag("passable");
}

void AssetInfo::set_ray_strength(int strength) {
        int clamped = std::clamp(strength, 0, 100);
        ray_strength = clamped;
        info_json_["ray_strength"] = ray_strength;
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

void AssetInfo::set_children(const std::vector<ChildInfo>& new_children) {

    children = new_children;

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& c : new_children) {
        nlohmann::json entry;
        entry["area_name"] = c.area_name;
        entry["z_offset"] = c.z_offset;

        try {
            if (c.inline_assets.is_array() && !c.inline_assets.empty()) {
                entry["spawn_groups"] = c.inline_assets;
            } else if (!c.json_path.empty()) {

                std::string rel = c.json_path;
                try {

                    std::string base = info_json_path_;
                    auto pos = base.find_last_of("/\\");
                    if (pos != std::string::npos) {
                        base = base.substr(0, pos);
                        if (rel.rfind(base, 0) == 0) {

                            size_t cut = base.size();
                            if (rel.size() > cut && (rel[cut] == '/' || rel[cut] == '\\')) ++cut;
                            rel = rel.substr(cut);
                        }
                    }
                } catch (...) {

                }
                entry["json_path"] = rel;
            }
        } catch (...) {

        }
        arr.push_back(std::move(entry));
    }
    info_json_["child_assets"] = std::move(arr);
}

void AssetInfo::set_lighting(bool is_shaded_,
                             const LightSource& shading,
                             int shading_factor,
                             const std::vector<LightSource>& lights) {
    is_shaded = is_shaded_;
    this->shading_factor = shading_factor;
    orbital_light_sources.clear();
    light_sources = lights;
    if (is_shaded) {
        orbital_light_sources.push_back(shading);
    }
    is_light_source = is_shaded || !lights.empty();

    nlohmann::json arr = nlohmann::json::array();

    nlohmann::json shade_entry = nlohmann::json::object();
    shade_entry["has_light_source"] = true;
    if (is_shaded) {
        shade_entry["light_intensity"] = shading.intensity;
        shade_entry["radius"] = shading.radius;
        const double f = std::max(0.01, static_cast<double>(shading_factor) / 100.0);
        int base_x = static_cast<int>(std::round(shading.x_radius / f));
        int base_y = static_cast<int>(std::round(shading.y_radius / f));
        int base_off_x = static_cast<int>(std::round(shading.offset_x / f));
        int base_off_y = static_cast<int>(std::round(shading.offset_y / f));
        shade_entry["x_radius"] = base_x;
        shade_entry["y_radius"] = base_y;
        shade_entry["falloff"] = shading.fall_off;
        shade_entry["offset_x"] = base_off_x;
        shade_entry["offset_y"] = base_off_y;
        shade_entry["factor"] = shading_factor;
        shade_entry["apex_speed_bias"] = shading.apex_speed_bias;
        shade_entry["behind"] = shading.behind;
    } else {
        shade_entry["light_intensity"] = 0;
        shade_entry["radius"] = 0;
        shade_entry["x_radius"] = 0;
        shade_entry["y_radius"] = 0;
        shade_entry["falloff"] = 0;
        shade_entry["offset_x"] = 0;
        shade_entry["offset_y"] = 0;
        shade_entry["factor"] = shading_factor;
        shade_entry["apex_speed_bias"] = shading.apex_speed_bias;
        shade_entry["behind"] = shading.behind;
    }
    arr.push_back(shade_entry);

    for (const auto& l : lights) {
        nlohmann::json j;
        j["has_light_source"] = true;
        j["light_intensity"] = l.intensity;
        j["radius"] = l.radius;
        j["falloff"] = l.fall_off;
        j["flicker"] = l.flicker;
        j["flare"] = l.flare;
        j["offset_x"] = l.offset_x;
        j["offset_y"] = l.offset_y;
        j["light_color"] = { l.color.r, l.color.g, l.color.b };
        j["behind"] = l.behind;
        arr.push_back(std::move(j));
    }
    info_json_["has_shading"] = is_shaded;
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
        if (info_json_path_.empty()) {
                return false;
        }

        std::ifstream in(info_json_path_);
        if (!in.is_open()) {
                return false;
        }

        nlohmann::json data = nlohmann::json::object();
        try {
                in >> data;
        } catch (...) {
                return false;
        }

        if (!data.is_object()) {
                return false;
        }

        nlohmann::json animations_section = nlohmann::json::object();
        auto animations_it = data.find("animations");
        if (animations_it != data.end()) {
                animations_section = *animations_it;
        }

        info_json_["animations"] = animations_section;

        const nlohmann::json* payloads = nullptr;
        if (animations_section.is_object()) {
                payloads = &animations_section;
                auto nested = animations_section.find("animations");
                if (nested != animations_section.end() && nested->is_object()) {
                        payloads = &(*nested);
                }
        }

        if (payloads && payloads->is_object()) {
                anims_json_ = *payloads;
        } else {
                anims_json_ = nlohmann::json::object();
        }

        std::string new_start = start_animation;
        if (animations_section.is_object()) {
                auto start_it = animations_section.find("start");
                if (start_it != animations_section.end() && start_it->is_string()) {
                        new_start = start_it->get<std::string>();
                }
        }
        if (new_start.empty() && data.contains("start") && data["start"].is_string()) {
                new_start = data["start"].get<std::string>();
        }

        start_animation = new_start;
        info_json_["start"] = start_animation;

        return true;
}
