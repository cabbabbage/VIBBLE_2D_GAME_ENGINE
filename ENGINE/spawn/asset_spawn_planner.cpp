#include "asset_spawn_planner.hpp"
#include <random>
#include <algorithm>
#include <unordered_set>
#include <cctype>
#include <iomanip>

#include "dev_mode/spawn_group_config/spawn_group_utils.hpp"
#include "room_relative_size_resolver.hpp"
#include "asset/Asset.hpp"
AssetSpawnPlanner::AssetSpawnPlanner(const std::vector<nlohmann::json>& json_sources,
                                     const Area& area,
                                     AssetLibrary& asset_library,
                                     const std::vector<SourceContext>& source_contexts)
: asset_library_(&asset_library) {
    source_jsons_ = json_sources;
    source_contexts_ = source_contexts;
    if (source_contexts_.size() < source_jsons_.size()) {
        source_contexts_.resize(source_jsons_.size());
    }
    source_changed_.assign(source_jsons_.size(), false);

    nlohmann::json merged;
    merged["spawn_groups"] = nlohmann::json::array();
    for (size_t si = 0; si < source_jsons_.size(); ++si) {
        try {
            nlohmann::json js = source_jsons_[si];
            auto& groups = devmode::spawn::ensure_spawn_groups_array(js);
            if (!groups.is_array()) continue;
            for (size_t ai = 0; ai < groups.size(); ++ai) {
                merged["spawn_groups"].push_back(groups[ai]);
                SourceRef ref;
                ref.source_index = static_cast<int>(si);
                ref.entry_index  = static_cast<int>(ai);
                ref.key = "spawn_groups";
                assets_provenance_.push_back(std::move(ref));
            }
        } catch (...) {

            continue;
        }
    }

    root_json_ = std::move(merged);
    parse_asset_spawns(area);
    sort_spawn_queue();
    persist_sources();
}

const std::vector<SpawnInfo>& AssetSpawnPlanner::get_spawn_queue() const {
    return spawn_queue_;
}

void AssetSpawnPlanner::parse_asset_spawns(const Area& area) {
    std::mt19937 rng(std::random_device{}());
    if (!root_json_.contains("spawn_groups") || !root_json_["spawn_groups"].is_array()) return;

    auto get_opt_str = [](const nlohmann::json& j, const char* k) -> std::string {
        return (j.contains(k) && j[k].is_string()) ? j[k].get<std::string>() : std::string{};
};

    for (size_t idx = 0; idx < root_json_["spawn_groups"].size(); ++idx) {
        auto& entry = root_json_["spawn_groups"][idx];
        if (!entry.is_object()) continue;
        nlohmann::json asset = entry;

        std::string spawn_id = get_opt_str(asset, "spawn_id");
        if (spawn_id.empty()) {
            spawn_id = devmode::spawn::generate_spawn_id();
            entry["spawn_id"] = spawn_id;
            asset["spawn_id"] = spawn_id;
            if (idx < assets_provenance_.size()) {
                const auto& ref = assets_provenance_[idx];
                if (auto* src = get_source_entry(ref.source_index, ref.entry_index, ref.key)) {
                    (*src)["spawn_id"] = spawn_id;
                    if (ref.source_index >= 0 && static_cast<size_t>(ref.source_index) < source_changed_.size()) {
                        source_changed_[static_cast<size_t>(ref.source_index)] = true;
                    }
                }
            }
        }

        int priority = -1;
        if (asset.contains("priority") && asset["priority"].is_number_integer()) {
            priority = asset["priority"].get<int>();
        }
        if (priority < 0) {
            priority = static_cast<int>(idx);
            entry["priority"] = priority;
            if (idx < assets_provenance_.size()) {
                const auto& ref = assets_provenance_[idx];
                if (auto* src = get_source_entry(ref.source_index, ref.entry_index, ref.key)) {
                    (*src)["priority"] = priority;
                    if (ref.source_index >= 0 && static_cast<size_t>(ref.source_index) < source_changed_.size()) {
                        source_changed_[static_cast<size_t>(ref.source_index)] = true;
                    }
                }
            }
        }

        std::string position = get_opt_str(asset, "position");
        if (position.empty()) position = "Random";
        if (position == "Exact Position") position = "Exact";

        std::string display_name = get_opt_str(asset, "display_name");
        if (display_name.empty()) display_name = get_opt_str(asset, "name");
        if (display_name.empty()) display_name = spawn_id;

        std::string link_name = get_opt_str(asset, "link");

        const bool force_single_quantity = (position == "Exact");

        auto read_bool = [](const nlohmann::json& j, const char* key, bool fallback) {
            if (!j.is_object() || !j.contains(key)) return fallback;
            const auto& value = j.at(key);
            if (value.is_boolean()) return value.get<bool>();
            if (value.is_number_integer()) return value.get<int>() != 0;
            if (value.is_string()) {
                std::string text = value.get<std::string>();
                std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (text == "true" || text == "1" || text == "yes") return true;
                if (text == "false" || text == "0" || text == "no") return false;
            }
            return fallback;
};

        auto ensure_bool_field = [&](const char* key, bool value) {
            bool updated = false;
            if (!entry.contains(key) || !entry[key].is_boolean() || entry[key].get<bool>() != value) {
                entry[key] = value;
                updated = true;
            }
            if (!asset.contains(key) || !asset[key].is_boolean() || asset[key].get<bool>() != value) {
                asset[key] = value;
                updated = true;
            }
            if (updated && idx < assets_provenance_.size()) {
                const auto& ref = assets_provenance_[idx];
                if (auto* src = get_source_entry(ref.source_index, ref.entry_index, ref.key)) {
                    (*src)[key] = value;
                    if (ref.source_index >= 0 && static_cast<size_t>(ref.source_index) < source_changed_.size()) {
                        source_changed_[static_cast<size_t>(ref.source_index)] = true;
                    }
                }
            }
};

        const bool default_geometry = (position == "Exact" || position == "Perimeter");
        const bool resolve_geometry = read_bool(asset, "resolve_geometry_to_room_size", default_geometry);
        const bool resolve_quantity = read_bool(asset, "resolve_quantity_to_room_size", false);

        ensure_bool_field("resolve_geometry_to_room_size", resolve_geometry);
        ensure_bool_field("resolve_quantity_to_room_size", resolve_quantity);

        auto [minx, miny, maxx, maxy] = area.get_bounds();
        const int curr_w = std::max(1, maxx - minx);
        const int curr_h = std::max(1, maxy - miny);

        int min_num = asset.value("min_number", 1);
        int max_num = asset.value("max_number", min_num);
        if (min_num < 0) min_num = 0;
        if (max_num < 0) max_num = 0;
        if (max_num < min_num) std::swap(max_num, min_num);

        int orig_w = asset.value("origional_width", curr_w);
        int orig_h = asset.value("origional_height", curr_h);

        const bool need_orig = default_geometry || resolve_geometry || resolve_quantity;
        if (need_orig) {
            bool set_wh = false;
            if (!asset.contains("origional_width") || !asset["origional_width"].is_number_integer()) {
                entry["origional_width"] = curr_w;
                asset["origional_width"] = curr_w;
                orig_w = curr_w;
                set_wh = true;
            } else {
                orig_w = asset["origional_width"].get<int>();
            }
            if (!asset.contains("origional_height") || !asset["origional_height"].is_number_integer()) {
                entry["origional_height"] = curr_h;
                asset["origional_height"] = curr_h;
                orig_h = curr_h;
                set_wh = true;
            } else {
                orig_h = asset["origional_height"].get<int>();
            }
            if (set_wh && idx < assets_provenance_.size()) {
                const auto& ref = assets_provenance_[idx];
                if (auto* src = get_source_entry(ref.source_index, ref.entry_index, ref.key)) {
                    (*src)["origional_width"] = entry["origional_width"];
                    (*src)["origional_height"] = entry["origional_height"];
                    if (ref.source_index >= 0 && static_cast<size_t>(ref.source_index) < source_changed_.size()) {
                        source_changed_[static_cast<size_t>(ref.source_index)] = true;
                    }
                }
            }
        }

        RoomRelativeSizeResolver scaler(orig_w, orig_h, curr_w, curr_h);
        if (resolve_quantity && !force_single_quantity) {
            auto scaled_range = scaler.scale_count_range(min_num, max_num);
            min_num = scaled_range.first;
            max_num = scaled_range.second;
        }

        int quantity = std::uniform_int_distribution<int>(min_num, max_num)(rng);
        if (force_single_quantity) {
            quantity = 1;
        }

        bool explicit_flip = false;
        bool force_flipped = false;
        try {
            const auto it_exp = asset.find("explicit_flip");
            if (it_exp != asset.end()) {
                if (it_exp->is_boolean()) explicit_flip = it_exp->get<bool>();
                else if (it_exp->is_number_integer()) explicit_flip = it_exp->get<int>() != 0;
            }
            const auto it_force = asset.find("force_flipped");
            if (it_force != asset.end()) {
                if (it_force->is_boolean()) force_flipped = it_force->get<bool>();
                else if (it_force->is_number_integer()) force_flipped = it_force->get<int>() != 0;
            }
        } catch (...) {}
        Asset::SetFlipOverrideForSpawnId(spawn_id, explicit_flip, force_flipped);

        std::vector<nlohmann::json> cand_jsons;
        if (asset.contains("candidates") && asset["candidates"].is_array()) {
            for (const auto& c : asset["candidates"]) cand_jsons.push_back(c);
        } else {
            nlohmann::json fallback;
            if (asset.contains("name") && asset["name"].is_string()) {
                fallback["name"] = asset["name"].get<std::string>();
            }
            fallback["chance"] = 100;
            cand_jsons.push_back(std::move(fallback));
        }
        if (cand_jsons.empty()) continue;

        std::vector<SpawnCandidate> candidates;
        candidates.reserve(cand_jsons.size());

        struct CandidateDraft {
            double weight = 0.0;
            bool use_tag = false;
            std::string tag;
            std::string original_name;
            std::string asset_name;
            std::string label;
            bool is_null = false;
};

        auto extract_chance = [](const nlohmann::json& c) -> double {
            if (!c.contains("chance")) return 0.0;
            const auto& chance = c["chance"];
            if (chance.is_number()) {
                return chance.get<double>();
            }
            return 0.0;
};

        auto sanitize_key = [](std::string value) {
            auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
            auto begin = std::find_if(value.begin(), value.end(), not_space);
            auto end = std::find_if(value.rbegin(), value.rend(), not_space).base();
            if (begin >= end) return std::string{};
            value.assign(begin, end);
            if (!value.empty() && value.front() == '#') value.erase(0, 1);
            return value;
};

        std::vector<CandidateDraft> drafts;
        drafts.reserve(cand_jsons.size());

        std::unordered_set<std::string> blocked_tags;
        std::unordered_set<std::string> blocked_assets;

        auto parse_candidate = [&](const nlohmann::json& cj) {
            CandidateDraft draft{};
            draft.weight = extract_chance(cj);

            bool is_null = cj.is_null();
            std::string name;
            std::string label;
            bool use_tag = false;
            std::string tag_value;

            auto detect_tag = [&](const std::string& v) {
                if (!v.empty() && v.front() == '#') {
                    use_tag = true;
                    tag_value = v.substr(1);
                }
};

            if (cj.is_object()) {
                if (cj.contains("name") && cj["name"].is_string()) {
                    name = cj["name"].get<std::string>();
                    detect_tag(name);
                }
                if (cj.contains("display_name") && cj["display_name"].is_string()) {
                    label = cj["display_name"].get<std::string>();
                } else if (cj.contains("label") && cj["label"].is_string()) {
                    label = cj["label"].get<std::string>();
                }
                if (cj.contains("tag")) {
                    const auto& tj = cj["tag"];
                    if (tj.is_boolean() && tj.get<bool>()) {
                        use_tag = true;
                        if (tag_value.empty() && !name.empty()) {
                            tag_value = name.front() == '#' ? name.substr(1) : name;
                        }
                    } else if (tj.is_string()) {
                        use_tag = true;
                        tag_value = tj.get<std::string>();
                    }
                }
                if (cj.contains("tag_name") && cj["tag_name"].is_string()) {
                    use_tag = true;
                    tag_value = cj["tag_name"].get<std::string>();
                }
            } else if (cj.is_string()) {
                name = cj.get<std::string>();
                detect_tag(name);
                label = name;
            }

            if (name == "null") is_null = true;

            if (use_tag && tag_value.empty() && !name.empty()) {
                tag_value = name.front() == '#' ? name.substr(1) : name;
            }

            draft.use_tag = use_tag;
            draft.tag = sanitize_key(tag_value);
            draft.original_name = name;
            draft.label = label;
            draft.is_null = is_null;

            if (!draft.use_tag) {
                std::string sanitized = sanitize_key(name);
                if (sanitized == "null") {
                    draft.is_null = true;
                    sanitized.clear();
                }
                draft.asset_name = sanitized;
            }

            return draft;
};

        for (const auto& cj : cand_jsons) {
            CandidateDraft draft = parse_candidate(cj);

            if (draft.weight <= 0.0) {
                if (draft.use_tag) {
                    if (!draft.tag.empty()) blocked_tags.insert(draft.tag);
                } else if (!draft.is_null) {
                    std::string blocked = !draft.asset_name.empty() ? draft.asset_name : sanitize_key(draft.original_name);
                    if (!blocked.empty()) blocked_assets.insert(blocked);
                }
            }

            drafts.push_back(std::move(draft));
        }

        std::unordered_set<std::string> candidate_tags;
        for (const auto& d : drafts) {
            if (d.use_tag && !d.tag.empty() && d.weight > 0.0) {
                candidate_tags.insert(d.tag);
            }
        }

        for (const auto& draft : drafts) {
            SpawnCandidate c{};
            c.weight = draft.weight;

            std::string resolved_name;
            if (draft.use_tag) {
                std::string tag = !draft.tag.empty() ? draft.tag : sanitize_key(draft.original_name);
                if (!tag.empty() && draft.weight > 0.0) {
                    try {
                        resolved_name = resolve_asset_from_tag( tag, &blocked_tags, &blocked_assets, &candidate_tags );
                    } catch (...) {
                        resolved_name.clear();
                    }
                }
            } else {
                resolved_name = draft.asset_name;
            }

            bool is_null = draft.is_null;
            if (draft.use_tag && draft.weight <= 0.0) {
                is_null = true;
            }

            if (!resolved_name.empty()) {
                c.name = resolved_name;
            } else if (!draft.use_tag) {
                c.name = draft.asset_name;
            }

            if (c.name.empty()) {
                if (!is_null) is_null = true;
            }

            std::string fallback_display = draft.original_name;
            if (fallback_display.empty() && !draft.tag.empty()) {
                fallback_display = "#" + draft.tag;
            }

            c.display_name = !draft.label.empty() ? draft.label : (!c.name.empty() ? c.name : fallback_display);

            if (c.display_name.empty() && is_null) c.display_name = "null";

            c.is_null = is_null || c.name.empty();
            if (!c.is_null && !c.name.empty()) {
                auto info = asset_library_->get(c.name);
                if (!info) c.is_null = true;
                else c.info = info;
            }

            if (c.is_null && c.display_name.empty()) c.display_name = "null";
            if (c.weight < 0.0) c.weight = 0.0;

            candidates.push_back(std::move(c));
        }
        if (candidates.empty()) continue;

        auto average_range = [&](const std::string& lo_key, const std::string& hi_key, int fallback) {
            int lo = asset.value(lo_key, fallback);
            int hi = asset.value(hi_key, fallback);
            if (lo == fallback && hi != fallback) return hi;
            if (hi == fallback && lo != fallback) return lo;
            return (lo + hi) / 2;
};

        SpawnInfo s{};
        s.name     = display_name;
        s.position = position;
        s.spawn_id = spawn_id;
        s.quantity = quantity;
        s.priority = priority;
        s.grid_resolution = entry.value("grid_resolution", 0);
        s.adjust_geometry_to_room = resolve_geometry;
        if (!link_name.empty()) {
            s.link_area_name = link_name;
        }

        s.check_min_spacing = asset.value("enforce_spacing", false);

        s.exact_offset.x = asset.value("dx", asset.value("exact_dx", 0));
        s.exact_offset.y = asset.value("dy", asset.value("exact_dy", 0));
        if (resolve_geometry) {
            s.exact_origin_w = orig_w;
            s.exact_origin_h = orig_h;
        } else {
            s.exact_origin_w = curr_w;
            s.exact_origin_h = curr_h;
        }
        s.exact_point.x  = asset.value("ep_x", average_range("ep_x_min", "ep_x_max", -1));
        s.exact_point.y  = asset.value("ep_y", average_range("ep_y_min", "ep_y_max", -1));

        if (position == "Perimeter") {
            int base_radius = asset.value("radius", asset.value("perimeter_radius", 0));
            s.perimeter_radius = resolve_geometry ? scaler.scale_length(base_radius) : base_radius;
        } else if (position == "Edge") {
            int inset = asset.value("edge_inset_percent", asset.value("boundary_inset", 100));
            inset = std::clamp(inset, 0, 200);
            s.edge_inset_percent = inset;
        }

        s.candidates = std::move(candidates);
        spawn_queue_.push_back(std::move(s));
    }
}

void AssetSpawnPlanner::sort_spawn_queue() {

    std::stable_sort(spawn_queue_.begin(), spawn_queue_.end(),
                     [](const SpawnInfo& a, const SpawnInfo& b){ return a.priority < b.priority; });
}

void AssetSpawnPlanner::persist_sources() {
    for (size_t i = 0; i < source_jsons_.size(); ++i) {
        if (i >= source_changed_.size() || !source_changed_[i]) continue;
        if (i >= source_contexts_.size()) continue;
        auto& ctx = source_contexts_[i];
        if (ctx.json_ref) {
            *ctx.json_ref = source_jsons_[i];
        }
        if (ctx.persist) {
            ctx.persist(source_jsons_[i]);
        }
    }
}

nlohmann::json* AssetSpawnPlanner::get_source_entry(int source_index, int entry_index, const std::string& key) {
    if (source_index < 0 || entry_index < 0) return nullptr;
    size_t si = static_cast<size_t>(source_index);
    size_t ei = static_cast<size_t>(entry_index);
    if (si >= source_jsons_.size()) return nullptr;
    try {
        auto& src = source_jsons_[si];
        if (!src.contains(key) || !src[key].is_array()) return nullptr;
        auto& arr = src[key];
        if (ei >= arr.size()) return nullptr;
        return &arr[ei];
    } catch (...) {
        return nullptr;
    }
}
std::string AssetSpawnPlanner::resolve_asset_from_tag(
    const std::string& tag,
    const std::unordered_set<std::string>* banned_tags,
    const std::unordered_set<std::string>* banned_assets,
    const std::unordered_set<std::string>* candidate_tags)
{
    static std::mt19937 rng(std::random_device{}());
    if (tag.empty()) {
        throw std::runtime_error("Empty tag provided to resolve_asset_from_tag");
    }

    std::vector<std::string> matches;
    for (const auto& [name, info] : asset_library_->all()) {
        if (!info || !info->has_tag(tag)) continue;

        if (banned_assets && banned_assets->count(name) > 0) continue;

        if (banned_tags && !banned_tags->empty()) {
            bool skip = false;
            for (const auto& blocked : *banned_tags) {
                if (blocked.empty()) continue;
                if (blocked == tag) continue;
                if (info->has_tag(blocked)) {
                    skip = true;
                    break;
                }
            }
            if (skip) continue;
        }

        if (candidate_tags && !candidate_tags->empty()) {
            bool skip = false;
            for (const auto& anti : info->anti_tags) {
                if (candidate_tags->count(anti) > 0 && anti != tag) {
                    skip = true;
                    break;
                }
            }
            if (skip) continue;
        }

        matches.push_back(name);
    }

    if (matches.empty()) {
        throw std::runtime_error("No assets found for tag: " + tag);
    }

    std::uniform_int_distribution<size_t> dist(0, matches.size() - 1);
    return matches[dist(rng)];
}
