#include "asset/asset_info.hpp"

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
std::vector<std::string> load_string_array(const nlohmann::json& json_value) {
    std::vector<std::string> values;
    if (!json_value.is_array()) {
        return values;
    }
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

void populate_from_json(AssetInfo& info, const nlohmann::json& raw_data) {
    nlohmann::json data = raw_data.is_object() ? raw_data : nlohmann::json::object();
    info.type = data.value("asset_type", std::string{});
    info.start_animation = data.value("start", std::string{});
    info.z_threshold = data.value("z_threshold", 0);
    info.passable = data.value("passable", false);
    info.min_same_type_distance = data.value("min_same_type_distance", 0);
    info.min_distance_all = data.value("min_distance_all", 0);

    info.tags = load_string_array(data.value("tags", nlohmann::json::array()));
    info.anti_tags = load_string_array(data.value("anti_tags", nlohmann::json::array()));

    AssetInfoTestAccess::rebuild_tag_cache(info);
    AssetInfoTestAccess::rebuild_anti_tag_cache(info);

    nlohmann::json tag_array = nlohmann::json::array();
    for (const auto& value : info.tags) {
        tag_array.push_back(value);
    }
    data["tags"] = std::move(tag_array);

    nlohmann::json anti_tag_array = nlohmann::json::array();
    for (const auto& value : info.anti_tags) {
        anti_tag_array.push_back(value);
    }
    data["anti_tags"] = std::move(anti_tag_array);

    AssetInfoTestAccess::initialize_info_json(info, std::move(data));

    info.passable = info.has_tag("passable");
}
} // namespace

AssetInfo::AssetInfo(const std::string& asset_folder_name)
    : is_shaded(false),
      is_light_source(false) {
    name = asset_folder_name;
    dir_path_ = "SRC/" + asset_folder_name;
    info_json_path_ = dir_path_ + "/info.json";

    std::ifstream in(info_json_path_);
    if (!in.is_open()) {
        throw std::runtime_error("Failed to open asset info: " + info_json_path_);
    }

    nlohmann::json data;
    in >> data;

    populate_from_json(*this, data);
}

AssetInfo::AssetInfo(const std::string& asset_folder_name, const nlohmann::json& metadata)
    : is_shaded(false),
      is_light_source(false) {
    nlohmann::json data = metadata.is_object() ? metadata : nlohmann::json::object();

    std::string resolved_name = data.value("asset_name", asset_folder_name);
    if (resolved_name.empty()) {
        resolved_name = asset_folder_name;
    }

    name = resolved_name;
    std::string fallback_dir = "SRC/" + resolved_name;
    dir_path_ = data.value("asset_directory", fallback_dir);
    if (dir_path_.empty()) {
        dir_path_ = fallback_dir;
    }
    info_json_path_.clear();

    populate_from_json(*this, data);
}

AssetInfo::~AssetInfo() = default;

bool AssetInfo::has_tag(const std::string& tag) const {
    return tag_lookup_.find(tag) != tag_lookup_.end();
}

void AssetInfo::set_tags(const std::vector<std::string>& t) {
    tags = t;
    rebuild_tag_cache();

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& value : tags) {
        arr.push_back(value);
    }
    info_json_["tags"] = std::move(arr);
    passable = has_tag("passable");
}

void AssetInfo::add_tag(const std::string& tag) {
    if (!tag.empty() && tag_lookup_.find(tag) == tag_lookup_.end()) {
        tags.push_back(tag);
    }
    set_tags(tags);
}

void AssetInfo::remove_tag(const std::string& tag) {
    tags.erase(std::remove(tags.begin(), tags.end(), tag), tags.end());
    set_tags(tags);
}

void AssetInfo::set_anti_tags(const std::vector<std::string>& t) {
    anti_tags = t;
    rebuild_anti_tag_cache();

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& value : anti_tags) {
        arr.push_back(value);
    }
    info_json_["anti_tags"] = std::move(arr);
}

void AssetInfo::add_anti_tag(const std::string& tag) {
    if (!tag.empty() && anti_tag_lookup_.find(tag) == anti_tag_lookup_.end()) {
        anti_tags.push_back(tag);
    }
    set_anti_tags(anti_tags);
}

void AssetInfo::remove_anti_tag(const std::string& tag) {
    anti_tags.erase(std::remove(anti_tags.begin(), anti_tags.end(), tag), anti_tags.end());
    set_anti_tags(anti_tags);
}

void AssetInfo::set_passable(bool v) {
    passable = v;
    if (v) {
        add_tag("passable");
    } else {
        remove_tag("passable");
    }
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
