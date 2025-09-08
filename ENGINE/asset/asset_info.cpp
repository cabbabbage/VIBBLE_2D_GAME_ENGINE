#include "asset_info.hpp"
#include "asset_info_methods/animation_loader.hpp"
#include "asset_info_methods/area_loader.hpp"
#include "asset_info_methods/child_loader.hpp"
#include "asset_info_methods/lighting_loader.hpp"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <random>
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
	load_base_properties(data);
	LightingLoader::load(*this, data);
	const auto &ss = data.value("size_settings", nlohmann::json::object());
	scale_factor = ss.value("scale_percentage", 100.0f) / 100.0f;
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
	type = data.value("asset_type", "Object");
	if (type == "Player") {
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

void AssetInfo::load_lighting_info(const nlohmann::json &data) {
	LightingLoader::load(*this, data);
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
	type = t;
	info_json_["asset_type"] = t;
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
	int scaled_canvas_w = static_cast<int>(original_canvas_width * scale);
	int scaled_canvas_h = static_cast<int>(original_canvas_height * scale);
	int offset_x = (scaled_canvas_w - 0) / 2;
	int offset_y = (scaled_canvas_h - 0);
	nlohmann::json points = nlohmann::json::array();
	for (const auto& p : area.get_points()) {
		double rel_x = (static_cast<double>(p.first)  - static_cast<double>(offset_x)) / static_cast<double>(scale);
		double rel_y = (static_cast<double>(p.second) - static_cast<double>(offset_y)) / static_cast<double>(scale);
		points.push_back({ rel_x, rel_y });
	}
	bool json_found = false;
	for (auto& entry : info_json_["areas"]) {
		if (entry.is_object() && entry.value("name", std::string{}) == area.get_name()) {
			entry["name"] = area.get_name();
			entry["points"] = std::move(points);
			json_found = true;
			break;
		}
	}
	if (!json_found) {
		nlohmann::json entry;
		entry["name"] = area.get_name();
		entry["points"] = std::move(points);
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
