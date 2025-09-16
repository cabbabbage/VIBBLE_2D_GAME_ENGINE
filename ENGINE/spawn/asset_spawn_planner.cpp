#include "asset_spawn_planner.hpp"
#include <iostream>
#include <sstream>
#include <fstream>
#include <random>
#include <algorithm>
#include <iomanip>
namespace fs = std::filesystem;

static std::string generate_spawn_id() {
	static std::mt19937 rng(std::random_device{}());
	static const char* hex = "0123456789abcdef";
	std::uniform_int_distribution<int> dist(0, 15);
	std::string s = "spn-";
	for (int i = 0; i < 12; ++i) s.push_back(hex[dist(rng)]);
	return s;
}

AssetSpawnPlanner::AssetSpawnPlanner(const std::vector<nlohmann::json>& json_sources,
                                     const Area& area,
                                     AssetLibrary& asset_library,
                                     const std::vector<std::string>& source_paths)
: asset_library_(&asset_library) {
	source_jsons_ = json_sources;
	source_paths_ = source_paths;
	if (source_paths_.size() < source_jsons_.size()) {
		source_paths_.resize(source_jsons_.size());
	}
	source_changed_.assign(source_jsons_.size(), false);
        nlohmann::json merged;
        merged["assets"] = nlohmann::json::array();
        for (size_t si = 0; si < source_jsons_.size(); ++si) {
                const auto& js = source_jsons_[si];
                if (js.contains("assets") && js["assets"].is_array()) {
                        for (size_t ai = 0; ai < js["assets"].size(); ++ai) {
                                merged["assets"].push_back(js["assets"][ai]);
                                assets_provenance_.emplace_back(static_cast<int>(si), static_cast<int>(ai));
                        }
                }
        }
        root_json_ = merged;
        parse_asset_spawns(area);
        sort_spawn_queue();
        persist_sources();
}

const std::vector<SpawnInfo>& AssetSpawnPlanner::get_spawn_queue() const {
        return spawn_queue_;
}

void AssetSpawnPlanner::parse_asset_spawns(const Area& area) {
        std::mt19937 rng(std::random_device{}());
        if (!root_json_.contains("assets") || !root_json_["assets"].is_array()) return;

        auto get_optional_string = [](const nlohmann::json& j, const std::string& key) -> std::string {
                if (j.contains(key) && j[key].is_string()) {
                        return j[key].get<std::string>();
                }
                return {};
        };

        for (size_t idx = 0; idx < root_json_["assets"].size(); ++idx) {
                auto& entry = root_json_["assets"][idx];
                if (!entry.is_object()) continue;
                nlohmann::json asset = entry;

                std::string spawn_id;
                if (asset.contains("spawn_id") && asset["spawn_id"].is_string()) {
                        spawn_id = asset["spawn_id"].get<std::string>();
                } else {
                        spawn_id = generate_spawn_id();
                        entry["spawn_id"] = spawn_id;
                        asset["spawn_id"] = spawn_id;
                        if (idx < assets_provenance_.size()) {
                                auto [si, ai] = assets_provenance_[idx];
                                if (si >= 0 && static_cast<size_t>(si) < source_jsons_.size()) {
                                        try {
                                                auto& src = source_jsons_[si];
                                                if (src.contains("assets") && src["assets"].is_array() && static_cast<size_t>(ai) < src["assets"].size()) {
                                                        src["assets"][ai]["spawn_id"] = spawn_id;
                                                        if (static_cast<size_t>(si) < source_changed_.size()) source_changed_[si] = true;
                                                }
                                        } catch (...) {}
                                }
                        }
                }

                std::string display_name = get_optional_string(asset, "spawn_group_name");
                if (display_name.empty() && asset.contains("spawn_group") && asset["spawn_group"].is_object()) {
                        display_name = get_optional_string(asset["spawn_group"], "name");
                }
                if (display_name.empty()) display_name = get_optional_string(asset, "group_name");
                if (display_name.empty()) display_name = get_optional_string(asset, "display_name");
                if (display_name.empty()) display_name = get_optional_string(asset, "name");
                if (display_name.empty()) display_name = spawn_id;

                int min_num = asset.value("min_number", 1);
                int max_num = asset.value("max_number", min_num);
                if (max_num < min_num) std::swap(max_num, min_num);
                int quantity = std::uniform_int_distribution<int>(min_num, max_num)(rng);

                std::string position = "Random";
                if (asset.contains("position")) {
                        if (asset["position"].is_string()) {
                                position = asset["position"].get<std::string>();
                        } else {
                                std::cerr << "[AssetSpawnPlanner] Entry with spawn_id '" << spawn_id
                                          << "' has non-string 'position'; defaulting to 'Random'.\n";
                        }
                }

                if (position == "Exact Position" || position == "Exact") {
                        auto [minx, miny, maxx, maxy] = area.get_bounds();
                        int curr_w = std::max(1, maxx - minx);
                        int curr_h = std::max(1, maxy - miny);
                        bool set_wh = false;
                        if (!asset.contains("exact_origin_width") || !asset["exact_origin_width"].is_number_integer()) {
                                entry["exact_origin_width"] = curr_w;
                                asset["exact_origin_width"] = curr_w;
                                set_wh = true;
                        }
                        if (!asset.contains("exact_origin_height") || !asset["exact_origin_height"].is_number_integer()) {
                                entry["exact_origin_height"] = curr_h;
                                asset["exact_origin_height"] = curr_h;
                                set_wh = true;
                        }
                        if (set_wh && idx < assets_provenance_.size()) {
                                auto [si, ai] = assets_provenance_[idx];
                                if (si >= 0 && static_cast<size_t>(si) < source_jsons_.size()) {
                                        try {
                                                auto& src = source_jsons_[si];
                                                if (src.contains("assets") && src["assets"].is_array() && static_cast<size_t>(ai) < src["assets"].size()) {
                                                        if (!src["assets"][ai].contains("exact_origin_width"))
                                                                src["assets"][ai]["exact_origin_width"] = entry["exact_origin_width"];
                                                        if (!src["assets"][ai].contains("exact_origin_height"))
                                                                src["assets"][ai]["exact_origin_height"] = entry["exact_origin_height"];
                                                        if (static_cast<size_t>(si) < source_changed_.size()) source_changed_[si] = true;
                                                }
                                        } catch (...) {}
                                }
                        }
                }

                auto gather_candidates = [](const nlohmann::json& asset_json) {
                        std::vector<nlohmann::json> out;
                        auto append_array = [&](const nlohmann::json& arr) {
                                for (const auto& cand : arr) out.push_back(cand);
                        };

                        if (asset_json.contains("spawn_group") && asset_json["spawn_group"].is_object()) {
                                const auto& group = asset_json["spawn_group"];
                                if (group.contains("candidates") && group["candidates"].is_array()) {
                                        append_array(group["candidates"]);
                                }
                        }
                        if (out.empty() && asset_json.contains("candidates") && asset_json["candidates"].is_array()) {
                                append_array(asset_json["candidates"]);
                        }
                        if (out.empty() && asset_json.contains("spawn_groups") && asset_json["spawn_groups"].is_array()) {
                                append_array(asset_json["spawn_groups"]);
                        }
                        if (out.empty() && asset_json.contains("spawn_group_candidates") && asset_json["spawn_group_candidates"].is_array()) {
                                append_array(asset_json["spawn_group_candidates"]);
                        }
                        if (out.empty()) {
                                nlohmann::json fallback;
                                if (asset_json.contains("name")) fallback["name"] = asset_json["name"];
                                if (asset_json.contains("tag")) fallback["tag"] = asset_json["tag"];
                                fallback["chance"] = 100;
                                out.push_back(fallback);
                        }
                        return out;
                };

                const auto candidate_entries = gather_candidates(asset);
                if (candidate_entries.empty()) continue;

                auto extract_weight = [](const nlohmann::json& cand) -> int {
                        if (!cand.is_object()) return 0;
                        if (cand.contains("chance") && cand["chance"].is_number_integer()) return cand["chance"].get<int>();
                        if (cand.contains("percent") && cand["percent"].is_number_integer()) return cand["percent"].get<int>();
                        if (cand.contains("weight") && cand["weight"].is_number_integer()) return cand["weight"].get<int>();
                        if (cand.contains("probability") && cand["probability"].is_number_integer()) return cand["probability"].get<int>();
                        return 0;
                };

                std::vector<SpawnCandidate> candidates;
                candidates.reserve(candidate_entries.size());
                for (const auto& cand_json : candidate_entries) {
                        SpawnCandidate candidate;
                        candidate.weight = extract_weight(cand_json);
                        bool is_null = cand_json.is_null();
                        std::string candidate_name;
                        std::string candidate_display;
                        bool use_tag = false;
                        std::string tag_value;

                        if (cand_json.is_object()) {
                                if (cand_json.contains("name") && cand_json["name"].is_string()) {
                                        candidate_name = cand_json["name"].get<std::string>();
                                }
                                if (cand_json.contains("asset") && cand_json["asset"].is_string()) {
                                        candidate_name = cand_json["asset"].get<std::string>();
                                }
                                if (cand_json.contains("display_name") && cand_json["display_name"].is_string()) {
                                        candidate_display = cand_json["display_name"].get<std::string>();
                                }
                                if (candidate_display.empty() && cand_json.contains("label") && cand_json["label"].is_string()) {
                                        candidate_display = cand_json["label"].get<std::string>();
                                }
                                if (cand_json.contains("tag")) {
                                        const auto& tag_json = cand_json["tag"];
                                        if (tag_json.is_boolean()) {
                                                if (tag_json.get<bool>()) {
                                                        use_tag = true;
                                                        if (tag_value.empty() && !candidate_name.empty()) tag_value = candidate_name;
                                                }
                                        } else if (tag_json.is_string()) {
                                                use_tag = true;
                                                tag_value = tag_json.get<std::string>();
                                        }
                                }
                                if (cand_json.contains("tag_name") && cand_json["tag_name"].is_string()) {
                                        use_tag = true;
                                        tag_value = cand_json["tag_name"].get<std::string>();
                                }
                        } else if (cand_json.is_string()) {
                                candidate_name = cand_json.get<std::string>();
                                candidate_display = candidate_name;
                        }

                        if (candidate_name == "null") {
                                is_null = true;
                        }

                        if (use_tag) {
                                std::string tag = !tag_value.empty() ? tag_value : candidate_name;
                                if (!tag.empty()) {
                                        nlohmann::json tag_entry;
                                        tag_entry["tag"] = tag;
                                        try {
                                                auto resolved = resolve_asset_from_tag(tag_entry);
                                                if (resolved.contains("name") && resolved["name"].is_string()) {
                                                        candidate_name = resolved["name"].get<std::string>();
                                                }
                                        } catch (...) {
                                                candidate_name.clear();
                                        }
                                }
                        }

                        candidate.display_name = !candidate_display.empty() ? candidate_display : candidate_name;
                        if (candidate.display_name.empty() && is_null) candidate.display_name = "null";

                        candidate.name = candidate_name;
                        candidate.is_null = is_null || candidate_name.empty();
                        if (!candidate.is_null) {
                                auto info = asset_library_->get(candidate.name);
                                if (!info) {
                                        candidate.is_null = true;
                                } else {
                                        candidate.info = info;
                                }
                        }
                        if (candidate.is_null && candidate.display_name.empty()) {
                                candidate.display_name = "null";
                        }
                        if (candidate.weight < 0) candidate.weight = 0;
                        candidates.push_back(std::move(candidate));
                }

                if (candidates.empty()) continue;

                SpawnInfo s;
                s.name = display_name;
                s.position = position;
                s.spawn_id = spawn_id;
                s.exact_offset.x = asset.value("exact_dx", 0);
                s.exact_offset.y = asset.value("exact_dy", 0);
                s.exact_origin_w = asset.value("exact_origin_width", 0);
                s.exact_origin_h = asset.value("exact_origin_height", 0);
                s.quantity = quantity;
                s.check_overlap = asset.value("check_overlap", false);
                s.check_min_spacing = asset.value("check_min_spacing", false);
                auto get_val = [&](const std::string& kmin, const std::string& kmax, int def) {
                        int vmin = asset.value(kmin, def);
                        int vmax = asset.value(kmax, def);
                        return (vmin + vmax) / 2;
                };
                s.grid_spacing = get_val("grid_spacing_min", "grid_spacing_max", 0);
                s.jitter = get_val("jitter_min", "jitter_max", 0);
                s.empty_grid_spaces = get_val("empty_grid_spaces_min", "empty_grid_spaces_max", 0);
                s.exact_point.x = get_val("ep_x_min", "ep_x_max", -1);
                s.exact_point.y = get_val("ep_y_min", "ep_y_max", -1);
                s.border_shift = get_val("border_shift_min", "border_shift_max", 0);
                s.sector_center = get_val("sector_center_min", "sector_center_max", 0);
                s.sector_range = get_val("sector_range_min", "sector_range_max", 0);
                s.perimeter_offset.x = get_val("perimeter_x_offset_min", "perimeter_x_offset_max", 0);
                s.perimeter_offset.y = get_val("perimeter_y_offset_min", "perimeter_y_offset_max", 0);
                s.percent_x_min = asset.value("percent_x_min", 0);
                s.percent_x_max = asset.value("percent_x_max", 0);
                s.percent_y_min = asset.value("percent_y_min", 0);
                s.percent_y_max = asset.value("percent_y_max", 0);
                s.candidates = std::move(candidates);
                spawn_queue_.push_back(std::move(s));
        }
}

void AssetSpawnPlanner::sort_spawn_queue() {
        const std::vector<std::string> priority_order = {
                "Center", "Entrance", "Exit", "Exact Position", "Perimeter"
        };
	auto to_lower = [](const std::string& s) {
		std::string result = s;
		std::transform(result.begin(), result.end(), result.begin(), ::tolower);
		return result;
	};
	auto priority_index = [&](const std::string& pos) -> int {
		std::string lower_pos = to_lower(pos);
		for (size_t i = 0; i < priority_order.size(); ++i) {
			if (to_lower(priority_order[i]) == lower_pos) {
					return static_cast<int>(i);
			}
		}
		return static_cast<int>(priority_order.size());
	};
	std::sort(spawn_queue_.begin(), spawn_queue_.end(), [&](const SpawnInfo& a, const SpawnInfo& b) {
		return priority_index(a.position) < priority_index(b.position);
           });
}

void AssetSpawnPlanner::persist_sources() {
	for (size_t i = 0; i < source_jsons_.size(); ++i) {
		if (i >= source_changed_.size() || !source_changed_[i]) continue;
		if (i >= source_paths_.size()) continue;
		const std::string& path = source_paths_[i];
		if (path.empty()) continue;
		try {
			std::ofstream out(path);
			if (out.is_open()) {
					out << source_jsons_[i].dump(2);
					out.close();
			}
		} catch (...) {
		}
	}
}

nlohmann::json AssetSpawnPlanner::resolve_asset_from_tag(const nlohmann::json& tag_entry) {
        static std::mt19937 rng(std::random_device{}());
        std::string tag;
        if (tag_entry.contains("tag") && tag_entry["tag"].is_string()) {
                tag = tag_entry["tag"].get<std::string>();
	} else {
		std::cerr << "[AssetSpawnPlanner] Missing or non-string 'tag' field when resolving asset.\n";
		tag.clear();
	}
	std::vector<std::string> matches;
	for (const auto& [name, info] : asset_library_->all()) {
		if (info && info->has_tag(tag)) {
			matches.push_back(name);
		}
	}
	if (matches.empty()) {
		throw std::runtime_error("No assets found for tag: " + tag);
	}
	std::uniform_int_distribution<size_t> dist(0, matches.size() - 1);
	std::string selected = matches[dist(rng)];
	nlohmann::json result = tag_entry;
        result["name"] = selected;
        result.erase("tag");
        return result;
}
