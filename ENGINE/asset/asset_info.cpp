#include "asset_info.hpp"
#include "asset_info_methods/animation_loader.hpp"
#include "asset/asset_types.hpp"
#include "asset_info_methods/area_loader.hpp"
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
AssetInfo::AssetInfo(const std::string &asset_folder_name)
: has_light_source(false) {
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
        int scaled_canvas_w = static_cast<int>(original_canvas_width * scale_factor);
        int scaled_canvas_h = static_cast<int>(original_canvas_height * scale_factor);
	int offset_x = (scaled_canvas_w - 0) / 2;
	int offset_y = (scaled_canvas_h - 0);
	load_areas(data, scale_factor, offset_x, offset_y);
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
		for (SDL_Texture *tex : anim.frames) {
			if (tex)
			SDL_DestroyTexture(tex);
		}
		anim.frames.clear();
	}
	animations.clear();
}

void AssetInfo::loadAnimations(SDL_Renderer *renderer) {
	AnimationLoader::load(*this, renderer);
}

void AssetInfo::load_base_properties(const nlohmann::json &data) {
        type = asset_types::canonicalize(data.value("asset_type", std::string{asset_types::object}));
        if (type == asset_types::player) {
                std::cout << "[AssetInfo] Player asset '" << name << "' loaded\n\n";
        }
	start_animation = data.value("start", std::string{"default"});
	z_threshold = data.value("z_threshold", 0);
	passable = has_tag("passable");
	has_shading = data.value("has_shading", false);
	min_same_type_distance = data.value("min_same_type_distance", 0);
	min_distance_all = data.value("min_distance_all", 0);
	flipable = data.value("can_invert", false);
}

bool AssetInfo::has_tag(const std::string &tag) const {
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
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
	nlohmann::json arr = nlohmann::json::array();
	for (const auto &s : tags)
	arr.push_back(s);
	info_json_["tags"] = std::move(arr);
	passable = has_tag("passable");
}

void AssetInfo::add_tag(const std::string &tag) {
	if (std::find(tags.begin(), tags.end(), tag) == tags.end()) {
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
        nlohmann::json arr = nlohmann::json::array();
        for (const auto &s : anti_tags)
                arr.push_back(s);
        info_json_["anti_tags"] = std::move(arr);
}

void AssetInfo::add_anti_tag(const std::string &tag) {
        if (std::find(anti_tags.begin(), anti_tags.end(), tag) == anti_tags.end()) {
                anti_tags.push_back(tag);
        }
        set_anti_tags(anti_tags);
}

void AssetInfo::remove_anti_tag(const std::string &tag) {
        anti_tags.erase(std::remove(anti_tags.begin(), anti_tags.end(), tag), anti_tags.end());
        set_anti_tags(anti_tags);
}

void AssetInfo::set_passable(bool v) {
	passable = v;
	if (v)
	add_tag("passable");
	else
	remove_tag("passable");
}

Area* AssetInfo::find_area(const std::string& name) {
	for (auto& na : areas) {
		if (na.name == name) return na.area.get();
	}
	return nullptr;
}
void AssetInfo::upsert_area_from_editor(const Area& area) {
	bool found = false;
	for (auto& na : areas) {
		if (na.name == area.get_name()) {
			na.area = std::make_unique<Area>(area);
			found = true;
			break;
		}
	}
	if (!found) {
		NamedArea na;
		na.name = area.get_name();
		na.area = std::make_unique<Area>(area);
		areas.push_back(std::move(na));
	}

	if (!info_json_.contains("areas") || !info_json_["areas"].is_array()) {
		info_json_["areas"] = nlohmann::json::array();
	}

	float scale = scale_factor;
	if (scale <= 0.0f) scale = 1.0f;

	auto compute_scaled = [](int dimension, float factor) {
		double value = static_cast<double>(dimension) * static_cast<double>(factor);
		long long rounded = std::llround(value);
		if (rounded < 1) rounded = 1;
		if (rounded > static_cast<long long>(std::numeric_limits<int>::max())) {
			return std::numeric_limits<int>::max();
		}
		return static_cast<int>(rounded);
	};

	const int scaled_canvas_w = compute_scaled(original_canvas_width, scale);
	const int scaled_canvas_h = compute_scaled(original_canvas_height, scale);

	const int default_offset_x = scaled_canvas_w / 2;
	const int default_offset_y = scaled_canvas_h;

	nlohmann::json* existing_entry = nullptr;
	int json_offset_x = 0;
	int json_offset_y = 0;

	for (auto& entry : info_json_["areas"]) {
		if (!entry.is_object()) continue;
		if (entry.value("name", std::string{}) == area.get_name()) {
			existing_entry = &entry;
			json_offset_x = entry.value("offset_x", 0);
			json_offset_y = entry.value("offset_y", 0);
			break;
		}
	}

	const int base_offset_x = default_offset_x + json_offset_x;
	const int base_offset_y = default_offset_y - json_offset_y;

	auto encode = [](double value) {
		double snapped = std::round(value * 1000.0) / 1000.0;
		if (std::abs(snapped) < 1e-6) {
			snapped = 0.0;
		}
		return snapped;
	};

	nlohmann::json points = nlohmann::json::array();
	for (const auto& p : area.get_points()) {
		double rel_x = (static_cast<double>(p.x) - static_cast<double>(base_offset_x)) / static_cast<double>(scale);
		double rel_y = (static_cast<double>(p.y) - static_cast<double>(base_offset_y)) / static_cast<double>(scale);
		points.push_back({ encode(rel_x), encode(rel_y) });
	}

	nlohmann::json original_dims = nlohmann::json::array({ original_canvas_width, original_canvas_height });

	if (existing_entry) {
		(*existing_entry)["name"] = area.get_name();
		(*existing_entry)["points"] = std::move(points);
		(*existing_entry)["original_dimensions"] = original_dims;
		(*existing_entry)["offset_x"] = json_offset_x;
		(*existing_entry)["offset_y"] = json_offset_y;
	} else {
		nlohmann::json entry;
		entry["name"] = area.get_name();
		entry["points"] = std::move(points);
		entry["original_dimensions"] = original_dims;
		entry["offset_x"] = json_offset_x;
		entry["offset_y"] = json_offset_y;
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

void AssetInfo::load_areas(const nlohmann::json& data, float scale, int offset_x,
                           int offset_y) {
	AreaLoader::load(*this, data, scale, offset_x, offset_y);
}

void AssetInfo::load_children(const nlohmann::json& data) {
    ChildLoader::load_children(*this, data, dir_path_);
}

void AssetInfo::set_children(const std::vector<ChildInfo>& new_children) {
    // Update in-memory copy
    children = new_children;
    // Serialize to JSON under "child_assets"
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& c : new_children) {
        nlohmann::json entry;
        entry["area_name"] = c.area_name;
        entry["z_offset"] = c.z_offset;
        // Prefer inline assets if present and non-empty; otherwise keep json_path if set
        try {
            if (c.inline_assets.is_array() && !c.inline_assets.empty()) {
                entry["assets"] = c.inline_assets;
            } else if (!c.json_path.empty()) {
                // c.json_path might be absolute (from loader). Try to store as relative to asset dir if possible.
                std::string rel = c.json_path;
                try {
                    // info_json_path_ is .../SRC/<AssetName>/info.json
                    std::string base = info_json_path_;
                    auto pos = base.find_last_of("/\\");
                    if (pos != std::string::npos) {
                        base = base.substr(0, pos); // asset folder
                        if (rel.rfind(base, 0) == 0) {
                            // Strip base + separator
                            size_t cut = base.size();
                            if (rel.size() > cut && (rel[cut] == '/' || rel[cut] == '\\')) ++cut;
                            rel = rel.substr(cut);
                        }
                    }
                } catch (...) {
                    // keep original rel
                }
                entry["json_path"] = rel;
            }
        } catch (...) {
            // ignore malformed inline assets
        }
        arr.push_back(std::move(entry));
    }
    info_json_["child_assets"] = std::move(arr);
}

void AssetInfo::set_lighting(bool has_shading_,
                             const LightSource& shading,
                             int shading_factor,
                             const std::vector<LightSource>& lights) {
    has_shading = has_shading_;
    this->shading_factor = shading_factor;
    orbital_light_sources.clear();
    light_sources = lights;
    if (has_shading) {
        orbital_light_sources.push_back(shading);
    }
    has_light_source = has_shading || !lights.empty();

    nlohmann::json arr = nlohmann::json::array();
    // Shading entry first
    nlohmann::json shade_entry = nlohmann::json::object();
    shade_entry["has_light_source"] = true;
    if (has_shading) {
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
    } else {
        shade_entry["light_intensity"] = 0;
        shade_entry["radius"] = 0;
        shade_entry["x_radius"] = 0;
        shade_entry["y_radius"] = 0;
        shade_entry["falloff"] = 0;
        shade_entry["offset_x"] = 0;
        shade_entry["offset_y"] = 0;
        shade_entry["factor"] = shading_factor;
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
        arr.push_back(std::move(j));
    }
    info_json_["has_shading"] = has_shading;
    info_json_["lighting_info"] = std::move(arr);
}

bool AssetInfo::remove_area(const std::string& name) {
    bool removed = false;
    // Remove from in-memory areas
    areas.erase(std::remove_if(areas.begin(), areas.end(), [&](const NamedArea& na){ return na.name == name; }), areas.end());
    // Remove from JSON
    try {
        if (info_json_.contains("areas") && info_json_["areas"].is_array()) {
            nlohmann::json new_arr = nlohmann::json::array();
            for (const auto& entry : info_json_["areas"]) {
                if (entry.is_object() && entry.value("name", std::string{}) == name) {
                    removed = true; // skip this one
                    continue;
                }
                new_arr.push_back(entry);
            }
            info_json_["areas"] = std::move(new_arr);
        }
    } catch (...) {
        // ignore JSON errors
    }
    return removed;
}

// ---------------- Animations editing (dev-mode UI) ----------------

std::vector<std::string> AssetInfo::animation_names() const {
	std::vector<std::string> names;
	try {
		if (info_json_.contains("animations") && info_json_["animations"].is_object()) {
			for (auto it = info_json_["animations"].begin(); it != info_json_["animations"].end(); ++it) {
				names.push_back(it.key());
			}
		}
	} catch (...) {
		// ignore
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
		// keep anims_json_ in sync (used by loader)
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
		// insert under new name then erase old
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
		// ignore
	}
}
