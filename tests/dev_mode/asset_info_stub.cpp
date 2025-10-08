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

    in >> info_json_;
    if (!info_json_.is_object()) {
        info_json_ = nlohmann::json::object();
    }

    type = info_json_.value("asset_type", std::string{});
    start_animation = info_json_.value("start", std::string{});
    z_threshold = info_json_.value("z_threshold", 0);
    passable = info_json_.value("passable", false);
    min_same_type_distance = info_json_.value("min_same_type_distance", 0);
    min_distance_all = info_json_.value("min_distance_all", 0);

    tags = load_string_array(info_json_.value("tags", nlohmann::json::array()));
    anti_tags = load_string_array(info_json_.value("anti_tags", nlohmann::json::array()));

    rebuild_tag_cache();
    rebuild_anti_tag_cache();

    if (!info_json_.contains("tags")) {
        info_json_["tags"] = nlohmann::json::array();
    }
    if (!info_json_.contains("anti_tags")) {
        info_json_["anti_tags"] = nlohmann::json::array();
    }

    passable = has_tag("passable");
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
