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
  info_json_ = data; // keep a snapshot for updates

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

  if (data.contains("animations")) {
    anims_json_ = data["animations"];
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

  z_threshold = data.value("z_threshold", 0);
  passable = has_tag("passable");

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

// --------------------- Update API ---------------------


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
  // Ensure size_settings exists
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
  // reflect in JSON
  nlohmann::json arr = nlohmann::json::array();
  for (const auto &s : tags)
    arr.push_back(s);
  info_json_["tags"] = std::move(arr);
  // update passable cache
  passable = has_tag("passable");
}

void AssetInfo::add_tag(const std::string &tag) {
  if (std::find(tags.begin(), tags.end(), tag) == tags.end()) {
    tags.push_back(tag);
  }
  set_tags(tags); // syncs json + passable
}

void AssetInfo::remove_tag(const std::string &tag) {
  tags.erase(std::remove(tags.begin(), tags.end(), tag), tags.end());
  set_tags(tags); // syncs json + passable
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

void AssetInfo::load_areas(const nlohmann::json& data, float scale, int offset_x,
                           int offset_y) {
  AreaLoader::load(*this, data, scale, offset_x, offset_y);
}

void AssetInfo::load_children(const nlohmann::json& data) {
  ChildLoader::load_children(*this, data, dir_path_);
}
