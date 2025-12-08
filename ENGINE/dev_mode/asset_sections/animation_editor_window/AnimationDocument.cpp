#include "AnimationDocument.hpp"

#include <SDL_log.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <fstream>
#include <nlohmann/json.hpp>
#include <unordered_set>
#include <unordered_map>

namespace {

using animation_editor::AnimationDocument;

bool parse_bool(const nlohmann::json& value, bool fallback) {
    if (value.is_boolean()) return value.get<bool>();
    if (value.is_number_integer()) return value.get<int>() != 0;
    if (value.is_number_float()) return value.get<double>() != 0.0;
    if (value.is_string()) {
        std::string text = value.get<std::string>();
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (text == "true" || text == "1" || text == "yes" || text == "on") return true;
        if (text == "false" || text == "0" || text == "no" || text == "off") return false;
    }
    return fallback;
}

bool parse_bool_field(const nlohmann::json& payload, const char* key, bool fallback) {
    if (!payload.is_object() || !key) {
        return fallback;
    }
    auto it = payload.find(key);
    if (it == payload.end()) {
        return fallback;
    }
    return parse_bool(*it, fallback);
}

int parse_int(const nlohmann::json& value, int fallback) {
    if (value.is_number_integer()) return value.get<int>();
    if (value.is_number()) return static_cast<int>(value.get<double>());
    if (value.is_string()) {
        try {
            return std::stoi(value.get<std::string>());
        } catch (...) {
        }
    }
    return fallback;
}

nlohmann::json coerce_payload(const std::string& animation_id, const nlohmann::json& source_payload) {
    nlohmann::json payload = source_payload.is_object() ? source_payload : nlohmann::json::object();

    nlohmann::json source = payload.contains("source") && payload["source"].is_object() ? payload["source"] : nlohmann::json::object();
    std::string kind = source.value("kind", std::string{"folder"});
    std::string path = source.value("path", kind == "folder" ? animation_id : std::string{});
    nlohmann::json name_value;
    if (kind == "folder") {
        // Use empty string so downstream UI code that calls get<std::string>() doesn't throw on null
        name_value = std::string{};
    } else {
        if (source.contains("name") && source["name"].is_string()) {
            name_value = source["name"].get<std::string>();
        } else {
            name_value = std::string{};
        }
    }
    payload["source"] = nlohmann::json{
        {"kind", kind},
        {"path", path},
        {"name", name_value},
};

    auto ensure_bool = [&](const char* key, bool fallback) {
        payload[key] = parse_bool(payload.contains(key) ? payload[key] : nlohmann::json(fallback), fallback);
};

    ensure_bool("flipped_source", false);
    ensure_bool("reverse_source", false);
    ensure_bool("locked", false);
    ensure_bool("loop", true);
    ensure_bool("rnd_start", false);

    bool derived_from_animation = (kind == "animation");
    bool derived_reverse = payload.value("reverse_source", false);
    bool derived_flip_x = payload.value("flipped_source", false);
    bool derived_flip_y = false;
    bool derived_flip_movement_x = false;
    bool derived_flip_movement_y = false;
    if (payload.contains("derived_modifiers") && payload["derived_modifiers"].is_object()) {
        const auto& modifiers = payload["derived_modifiers"];
        if (modifiers.contains("reverse")) {
            derived_reverse = parse_bool(modifiers["reverse"], derived_reverse);
        }
        if (modifiers.contains("flipX")) {
            derived_flip_x = parse_bool(modifiers["flipX"], derived_flip_x);
        }
        if (modifiers.contains("flipY")) {
            derived_flip_y = parse_bool(modifiers["flipY"], false);
        }
        if (modifiers.contains("flipMovementX")) {
            derived_flip_movement_x = parse_bool(modifiers["flipMovementX"], false);
        }
        if (modifiers.contains("flipMovementY")) {
            derived_flip_movement_y = parse_bool(modifiers["flipMovementY"], false);
        }
    }

    // Respect inherit_source_movement for derived animations. When a derived animation opts
    // OUT of inheriting movement, we keep local movement data in the payload so editors can
    // load/save it. Otherwise, we strip movement-related fields for clean derived records.
    bool inherit_source_movement = payload.value("inherit_source_movement", derived_from_animation);
    // Ensure the key is explicitly present for downstream tools/UI
    payload["inherit_source_movement"] = inherit_source_movement;

    if (derived_from_animation) {
        payload["derived_modifiers"] = nlohmann::json{{"reverse", derived_reverse},
                                                       {"flipX", derived_flip_x},
                                                       {"flipY", derived_flip_y},
                                                       {"flipMovementX", derived_flip_movement_x},
                                                       {"flipMovementY", derived_flip_movement_y}};
        // Keep movement only if not inheriting; otherwise strip to avoid conflicting data
        if (inherit_source_movement) {
            payload.erase("movement");
            payload.erase("movement_total");
            payload.erase("movement_variants");
        }
        // Keep other derived-compatible fields minimal
        payload.erase("audio");
        payload.erase("locked");
        payload.erase("movement_preview_bounds");
    } else {
        payload.erase("derived_modifiers");
    }
    payload["reverse_source"] = derived_reverse;
    payload["flipped_source"] = derived_flip_x;

    payload.erase("fps");
    double raw_speed = 1.0;
    try {
        if (payload.contains("speed_multiplier") && payload["speed_multiplier"].is_number()) {
            raw_speed = payload["speed_multiplier"].get<double>();
        } else if (payload.contains("speed_factor") && payload["speed_factor"].is_number()) {
            raw_speed = payload["speed_factor"].get<double>();
        }
    } catch (...) {
        raw_speed = 1.0;
    }
    const double kSpeedOptions[] = {0.25, 0.5, 1.0, 2.0, 4.0};
    double best_speed = 1.0;
    double best_diff = std::numeric_limits<double>::max();
    for (double opt : kSpeedOptions) {
        double diff = std::abs(opt - raw_speed);
        if (diff < best_diff) {
            best_diff = diff;
            best_speed = opt;
        }
    }
    if (!std::isfinite(raw_speed) || raw_speed <= 0.0) {
        best_speed = 1.0;
    }
    payload["speed_multiplier"] = best_speed;
    payload.erase("speed_factor");

    bool crop_frames = parse_bool_field(payload, "crop_frames", false);
    payload["crop_frames"] = crop_frames;
    if (crop_frames && payload.contains("crop_bounds") && payload["crop_bounds"].is_object()) {
        const auto& bounds = payload["crop_bounds"];
        auto read_bound = [&](const char* key) {
            if (bounds.contains(key)) {
                return parse_int(bounds.at(key), 0);
            }
            return 0;
        };
        int top = std::max(0, read_bound("top"));
        int left = std::max(0, read_bound("left"));
        int right = std::max(0, read_bound("right"));
        int bottom = std::max(0, read_bound("bottom"));
        int width = std::max(0, read_bound("width"));
        int height = std::max(0, read_bound("height"));
        nlohmann::json clean_bounds = {{"top", top}, {"left", left}, {"right", right}, {"bottom", bottom}};
        if (width > 0 && height > 0) {
            clean_bounds["width"] = width;
            clean_bounds["height"] = height;
        }
        payload["crop_bounds"] = clean_bounds;
    } else if (!crop_frames) {
        payload.erase("crop_bounds");
    }

    int frames = parse_int(payload.contains("number_of_frames") ? payload["number_of_frames"] : nlohmann::json(1), 1);
    if (frames < 1) frames = 1;
    payload["number_of_frames"] = frames;

    if (!derived_from_animation || (derived_from_animation && !inherit_source_movement)) {
        nlohmann::json movement = payload.contains("movement") && payload["movement"].is_array() ? payload["movement"] : nlohmann::json::array();
        if (!movement.is_array()) {
            movement = nlohmann::json::array();
        }
        if (movement.size() < static_cast<size_t>(frames)) {
            while (movement.size() < static_cast<size_t>(frames)) {
                movement.push_back(nlohmann::json::array({0, 0}));
            }
        } else if (movement.size() > static_cast<size_t>(frames)) {
            movement.erase(movement.begin() + frames, movement.end());
        }
        if (movement.empty()) {
            movement.push_back(nlohmann::json::array({0, 0}));
        }
        payload["movement"] = movement;

        auto read_component = [](const nlohmann::json& entry, int index) -> int {
            if (entry.is_array()) {
                if (index < static_cast<int>(entry.size()) && entry[index].is_number()) {
                    try {
                        return entry[index].get<int>();
                    } catch (...) {
                    }
                    try {
                        return static_cast<int>(entry[index].get<double>());
                    } catch (...) {
                    }
                }
                return 0;
            }
            if (entry.is_object()) {
                const char* keys[] = {"dx", "dy"};
                const char* key = (index == 0) ? keys[0] : keys[1];
                if (entry.contains(key)) {
                    return parse_int(entry[key], 0);
                }
            }
            return 0;
        };

        int total_dx = 0;
        int total_dy = 0;
        for (std::size_t i = 1; i < movement.size(); ++i) {
            const nlohmann::json& entry = movement[i];
            total_dx += read_component(entry, 0);
            total_dy += read_component(entry, 1);
        }
        payload["movement_total"] = nlohmann::json{{"dx", total_dx}, {"dy", total_dy}};
    } else {
        payload.erase("movement");
        payload.erase("movement_total");
    }

    std::string on_end = "default";
    if (payload.contains("on_end")) {
        if (payload["on_end"].is_string()) {
            on_end = payload["on_end"].get<std::string>();
        } else if (payload["on_end"].is_null()) {
            on_end = "default";
        }
    }
    payload["on_end"] = on_end;

    // Normalize children list: keep unique, non-empty strings in declared order
    if (payload.contains("children") && payload["children"].is_array()) {
        nlohmann::json dedup = nlohmann::json::array();
        std::unordered_set<std::string> seen;
        for (const auto& entry : payload["children"]) {
            if (!entry.is_string()) continue;
            std::string name = entry.get<std::string>();
            if (name.empty()) continue;
            if (seen.insert(name).second) {
                dedup.push_back(name);
            }
        }
        payload["children"] = std::move(dedup);
    }

    if (!derived_from_animation) {
        if (payload.contains("audio") && payload["audio"].is_object()) {
            auto audio = payload["audio"];
            std::string name = audio.value("name", std::string{});
            int volume = std::clamp(parse_int(audio.contains("volume") ? audio["volume"] : nlohmann::json(100), 100), 0, 100);
            bool effects = parse_bool(audio.contains("effects") ? audio["effects"] : nlohmann::json(false), false);
            if (!name.empty()) {
                payload["audio"] = nlohmann::json{{"name", name}, {"volume", volume}, {"effects", effects}};
            } else {
                payload.erase("audio");
            }
        } else {
            payload.erase("audio");
        }
    } else {
        payload.erase("audio");
    }

    return payload;
}

std::string serialize_payload(const nlohmann::json& payload) {
    return payload.dump();
}

nlohmann::json parse_payload(const std::string& payload_dump, const std::string& animation_id) {
    if (payload_dump.empty()) {
        return coerce_payload(animation_id, nlohmann::json::object());
    }
    nlohmann::json parsed = nlohmann::json::parse(payload_dump, nullptr, false);
    if (parsed.is_discarded()) {
        SDL_Log("AnimationDocument: failed to parse payload for '%s'", animation_id.c_str());
        return coerce_payload(animation_id, nlohmann::json::object());
    }
    return coerce_payload(animation_id, parsed);
}

}

namespace animation_editor {

AnimationDocument::AnimationDocument() = default;

void AnimationDocument::load_from_file(const std::filesystem::path& info_path) {
    info_path_ = info_path;
    asset_root_ = info_path.empty() ? std::filesystem::path{} : info_path.parent_path();
    persist_callback_ = nullptr;

    nlohmann::json root = nlohmann::json::object();
    if (!info_path.empty()) {
        std::ifstream in(info_path);
        if (in.good()) {
            try {
                in >> root;
            } catch (const std::exception& ex) {
                SDL_Log("AnimationDocument: failed to parse %s: %s", info_path.string().c_str(), ex.what());
                root = nlohmann::json::object();
            }
        }
    }
    if (!root.is_object()) {
        root = nlohmann::json::object();
    }

    base_data_ = root;
    load_from_json_object(base_data_);
}

void AnimationDocument::load_from_manifest(const nlohmann::json& asset_json,
                                           const std::filesystem::path& asset_root,
                                           std::function<void(const nlohmann::json&)> persist_callback) {
    info_path_.clear();
    asset_root_ = asset_root;
    persist_callback_ = std::move(persist_callback);
    base_data_ = asset_json.is_object() ? asset_json : nlohmann::json::object();
    load_from_json_object(base_data_);
}

void AnimationDocument::set_on_saved_callback(std::function<void()> callback) {
    on_saved_callback_ = std::move(callback);
}

void AnimationDocument::load_from_json_object(const nlohmann::json& root) {
    animations_.clear();
    start_animation_.reset();
    use_nested_container_ = false;
    container_metadata_.clear();
    dirty_ = false;

    nlohmann::json canonical = root.is_object() ? root : nlohmann::json::object();

    auto start_it = canonical.find("start");
    if (start_it != canonical.end() && start_it->is_string()) {
        std::string start_value = start_it->get<std::string>();
        if (!start_value.empty()) {
            start_animation_ = std::move(start_value);
        }
    }

    const auto animations_it = canonical.find("animations");
    if (animations_it != canonical.end()) {
        if (animations_it->is_object()) {
            const nlohmann::json* payloads = &(*animations_it);
            if (animations_it->contains("animations") && (*animations_it)["animations"].is_object()) {
                use_nested_container_ = true;
                nlohmann::json extras = *animations_it;
                extras.erase("animations");
                extras.erase("start");
                if (!extras.empty()) {
                    container_metadata_ = extras.dump();
                }
                payloads = &(*animations_it)["animations"];
                auto nested_start = animations_it->find("start");
                if (nested_start != animations_it->end() && nested_start->is_string()) {
                    std::string value = nested_start->get<std::string>();
                    if (!value.empty()) start_animation_ = std::move(value);
                }
            }

            for (const auto& item : payloads->items()) {
                if (!item.value().is_object()) {
                    if (item.key() == "start" && item.value().is_string()) {
                        std::string value = item.value().get<std::string>();
                        if (!value.empty()) start_animation_ = std::move(value);
                    }
                    continue;
                }
                animations_[item.key()] = serialize_payload(coerce_payload(item.key(), item.value()));
            }
        }
    }

    ensure_document_initialized();
}

void AnimationDocument::save_to_file(bool fire_callback) const {
    nlohmann::json root;
    if (persist_callback_) {
        root = base_data_.is_object() ? base_data_ : nlohmann::json::object();
    } else {
        root = nlohmann::json::object();
        if (!info_path_.empty()) {
            std::ifstream in(info_path_);
            if (in.good()) {
                try {
                    in >> root;
                } catch (const std::exception& ex) {
                    SDL_Log("AnimationDocument: failed to parse %s for saving: %s", info_path_.string().c_str(), ex.what());
                    root = nlohmann::json::object();
                }
            }
        }
        if (!root.is_object()) {
            root = nlohmann::json::object();
        }
        if (base_data_.is_object()) {
            // Keep any top-level metadata (animation_children, size_settings, etc.) in sync before overwriting "animations".
            for (auto it = base_data_.begin(); it != base_data_.end(); ++it) {
                if (it.key() == "animations" || it.key() == "start") {
                    continue;
                }
                root[it.key()] = it.value();
            }
        }
    }

    nlohmann::json animations_json = nlohmann::json::object();
    for (const auto& [id, payload_dump] : animations_) {
        animations_json[id] = parse_payload(payload_dump, id);
    }

    if (use_nested_container_) {
        nlohmann::json container = nlohmann::json::object();
        if (!container_metadata_.empty()) {
            nlohmann::json extras = nlohmann::json::parse(container_metadata_, nullptr, false);
            if (extras.is_object()) {
                for (auto& item : extras.items()) {
                    container[item.key()] = item.value();
                }
            }
        }
        container["animations"] = animations_json;
        container["start"] = start_animation_.has_value() ? *start_animation_ : std::string{};
        root["animations"] = container;
    } else {
        root["animations"] = animations_json;
        root["start"] = start_animation_.has_value() ? *start_animation_ : std::string{};
    }

    if (persist_callback_) {
        persist_callback_(root);
        base_data_ = root;
    } else {
        if (info_path_.empty()) {
            SDL_Log("AnimationDocument: no info path available for saving.");
            return;
        }
        std::ofstream out(info_path_);
        if (!out.good()) {
            SDL_Log("AnimationDocument: failed to open %s for writing", info_path_.string().c_str());
            return;
        }
        out << root.dump(4);
        base_data_ = root;
    }
    dirty_ = false;
    if (fire_callback && on_saved_callback_) {
        on_saved_callback_();
    }
}

bool AnimationDocument::consume_dirty_flag() const {
    if (!dirty_) {
        return false;
    }
    dirty_ = false;
    return true;
}

void AnimationDocument::create_animation(const std::string& animation_id) {
    std::string base = animation_id.empty() ? std::string{"animation"} : animation_id;
    std::string candidate = base;
    int suffix = 2;
    while (animations_.count(candidate) != 0) {
        candidate = base + "_" + std::to_string(suffix++);
    }

    nlohmann::json payload = coerce_payload(candidate, nlohmann::json::object({
                                                    {"source", nlohmann::json::object({
                                                                    {"kind", "folder"},
                                                                    {"path", candidate},
                                                                    {"name", nullptr},
                                                                })},
                                                }));
    animations_[candidate] = serialize_payload(payload);
    if (!start_animation_.has_value() || start_animation_->empty()) {
        start_animation_ = candidate;
    }
    rebuild_animation_cache();
    mark_dirty();
}

void AnimationDocument::delete_animation(const std::string& animation_id) {
    if (animation_id.empty()) return;
    auto it = animations_.find(animation_id);
    if (it == animations_.end()) return;
    animations_.erase(it);

    if (start_animation_ && *start_animation_ == animation_id) {
        auto ids = animation_ids();
        if (!ids.empty()) {
            start_animation_ = ids.front();
        } else {
            start_animation_.reset();
        }
    }
    mark_dirty();
}

std::vector<std::string> AnimationDocument::animation_ids() const {
    std::vector<std::string> ids;
    ids.reserve(animations_.size());
    for (const auto& entry : animations_) {
        ids.push_back(entry.first);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::optional<std::string> AnimationDocument::start_animation() const {
    if (!start_animation_ || start_animation_->empty()) return std::nullopt;
    if (animations_.count(*start_animation_) == 0) return std::nullopt;
    return start_animation_;
}

void AnimationDocument::set_start_animation(const std::string& animation_id) {
    if (animation_id.empty()) {
        if (start_animation_) {
            start_animation_.reset();
            mark_dirty();
        }
        return;
    }
    if (animations_.count(animation_id) == 0) {
        return;
    }
    if (!start_animation_ || *start_animation_ != animation_id) {
        start_animation_ = animation_id;
        mark_dirty();
    }
}

void AnimationDocument::rename_animation(const std::string& old_id, const std::string& new_id) {
    if (old_id.empty() || new_id.empty() || old_id == new_id) return;
    auto it = animations_.find(old_id);
    if (it == animations_.end()) return;

    std::string base = new_id;
    std::string candidate = base;
    int suffix = 2;
    while (animations_.count(candidate) != 0 && candidate != old_id) {
        candidate = base + "_" + std::to_string(suffix++);
    }

    if (candidate == old_id) {
        return;
    }

#if defined(__cpp_lib_node_extract)
    auto node = animations_.extract(old_id);
    node.key() = candidate;
    animations_.insert(std::move(node));
#else
    std::string payload = it->second;
    animations_.erase(it);
    animations_[candidate] = payload;
#endif

    if (start_animation_ && *start_animation_ == old_id) {
        start_animation_ = candidate;
    }
    // After the key change, walk all payloads and rewrite references that
    // point to the old id. This includes:
    //  - source.kind == "animation": update both name and fallback path
    //  - on_end fields equal to old id
    //  - any id-bearing entries in movement_variants if present
    for (auto& entry : animations_) {
        const std::string& id = entry.first;
        nlohmann::json payload = parse_payload(entry.second, id);

        bool changed = false;

        // Helper to trim a string for lenient comparisons
        auto trim_copy = [](std::string s) {
            auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
            s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
            s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
            return s;
        };

        // Update source references that may point to the renamed id
        if (payload.contains("source") && payload["source"].is_object()) {
            nlohmann::json& src = payload["source"];
            std::string kind = src.value("kind", std::string{"folder"});
            if (kind == std::string{"animation"}) {
                // Preferred explicit name
                if (src.contains("name")) {
                    if (src["name"].is_string()) {
                        std::string name = trim_copy(src["name"].get<std::string>());
                        if (name == old_id) {
                            src["name"] = candidate;
                            changed = true;
                        }
                    } else if (src["name"].is_null()) {
                        // keep fallback below
                    }
                }
                // Fallback path (used when name is null/empty)
                if (src.contains("path") && src["path"].is_string()) {
                    std::string path = trim_copy(src["path"].get<std::string>());
                    if (path == old_id) {
                        src["path"] = candidate;
                        changed = true;
                    }
                }
            }
        }

        // Update on_end transition if it references the old id
        if (payload.contains("on_end") && payload["on_end"].is_string()) {
            std::string oe = trim_copy(payload["on_end"].get<std::string>());
            if (oe == old_id) {
                payload["on_end"] = candidate;
                changed = true;
            }
        }

        // Update any id-bearing fields in movement_variants if present
        if (payload.contains("movement_variants")) {
            nlohmann::json& mv = payload["movement_variants"];
            // Best-effort: replace any string value equal to old_id anywhere within
            std::function<void(nlohmann::json&)> rewrite_strings = [&](nlohmann::json& node) {
                if (node.is_string()) {
                    try {
                        std::string v = node.get<std::string>();
                        if (trim_copy(v) == old_id) {
                            node = candidate;
                            changed = true;
                        }
                    } catch (...) {
                    }
                    return;
                }
                if (node.is_array()) {
                    for (auto& item : node) rewrite_strings(item);
                    return;
                }
                if (node.is_object()) {
                    for (auto it2 = node.begin(); it2 != node.end(); ++it2) {
                        rewrite_strings(it2.value());
                    }
                    return;
                }
            };
            rewrite_strings(mv);
        }

        if (changed) {
            entry.second = serialize_payload(coerce_payload(id, payload));
        }
    }

    // Keep UI/model caches coherent after cascading edits
    mark_dirty();
    rebuild_animation_cache();
}

void AnimationDocument::replace_animation_payload(const std::string& animation_id, const std::string& payload_json) {
    auto it = animations_.find(animation_id);
    if (it == animations_.end()) return;
    nlohmann::json parsed = nlohmann::json::parse(payload_json, nullptr, false);
    if (parsed.is_discarded()) {
        SDL_Log("AnimationDocument: ignoring invalid payload for '%s'", animation_id.c_str());
        return;
    }
    std::string normalized = serialize_payload(coerce_payload(animation_id, parsed));
    if (it->second == normalized) {
        return;
    }
    it->second = std::move(normalized);
    mark_dirty();
}

std::optional<std::string> AnimationDocument::animation_payload(const std::string& animation_id) const {
    auto it = animations_.find(animation_id);
    if (it == animations_.end()) return std::nullopt;
    return it->second;
}

namespace {
std::vector<std::string> parse_child_names(const nlohmann::json& value) {
    std::vector<std::string> names;
    if (!value.is_array()) {
        return names;
    }
    std::unordered_set<std::string> seen;
    for (const auto& entry : value) {
        if (!entry.is_string()) {
            continue;
        }
        std::string name = entry.get<std::string>();
        if (name.empty() || !seen.insert(name).second) {
            continue;
        }
        names.push_back(std::move(name));
    }
    return names;
}
}  // namespace

namespace {

std::vector<int> build_child_index_remap(const std::vector<std::string>& previous,
                                         const std::vector<std::string>& next) {
    std::vector<int> remap(previous.size(), -1);
    if (previous.empty()) {
        return remap;
    }
    std::unordered_map<std::string, int> next_lookup;
    next_lookup.reserve(next.size());
    for (size_t i = 0; i < next.size(); ++i) {
        next_lookup[next[i]] = static_cast<int>(i);
    }
    for (size_t i = 0; i < previous.size(); ++i) {
        auto it = next_lookup.find(previous[i]);
        if (it != next_lookup.end()) {
            remap[i] = it->second;
        }
    }
    return remap;
}

bool extract_child_index(const nlohmann::json& node, int& out_index) {
    if (node.is_object()) {
        auto it = node.find("child_index");
        if (it != node.end() && it->is_number_integer()) {
            out_index = it->get<int>();
            return true;
        }
    }
    if (node.is_array() && !node.empty()) {
        const auto& idx = node[0];
        if (idx.is_number_integer()) {
            out_index = idx.get<int>();
            return true;
        }
        if (idx.is_number()) {
            out_index = static_cast<int>(idx.get<double>());
            return true;
        }
    }
    return false;
}

bool sanitize_child_entries(nlohmann::json& container, const std::vector<int>& remap) {
    if (!container.is_array()) {
        const bool had_data = !container.is_null();
        container = nlohmann::json::array();
        return had_data;
    }
    if (container.empty()) {
        return false;
    }
    if (remap.empty()) {
        const bool had_entries = !container.empty();
        container = nlohmann::json::array();
        return had_entries;
    }
    nlohmann::json sanitized = nlohmann::json::array();
    sanitized.get_ref<nlohmann::json::array_t&>().reserve(container.size());
    bool changed = false;
    for (const auto& entry : container) {
        int old_index = -1;
        if (!extract_child_index(entry, old_index)) {
            changed = true;
            continue;
        }
        if (old_index < 0 || old_index >= static_cast<int>(remap.size())) {
            changed = true;
            continue;
        }
        const int new_index = remap[static_cast<std::size_t>(old_index)];
        if (new_index < 0) {
            changed = true;
            continue;
        }
        nlohmann::json updated = entry;
        if (entry.is_array()) {
            if (updated.empty()) {
                updated.push_back(new_index);
                changed = true;
            } else if (!updated[0].is_number_integer() || updated[0].get<int>() != new_index) {
                updated[0] = new_index;
                changed = true;
            }
        } else if (entry.is_object()) {
            int stored = updated.value("child_index", -1);
            if (stored != new_index) {
                updated["child_index"] = new_index;
                changed = true;
            }
        }
        sanitized.push_back(std::move(updated));
    }
    if (sanitized.size() != container.size()) {
        changed = true;
    }
    container = std::move(sanitized);
    return changed;
}

nlohmann::json* locate_child_array(nlohmann::json& entry) {
    if (!entry.is_array()) {
        return nullptr;
    }
    auto nested_array = [](nlohmann::json& value) -> nlohmann::json* {
        if (value.is_array() && !value.empty() && value[0].is_array()) {
            return &value;
        }
        return nullptr;
    };
    if (entry.size() > 4 && entry[4].is_array()) {
        return &entry[4];
    }
    if (entry.size() > 3) {
        if (auto* ptr = nested_array(entry[3])) {
            return ptr;
        }
    }
    if (entry.size() > 2) {
        if (auto* ptr = nested_array(entry[2])) {
            return ptr;
        }
    }
    return nullptr;
}

bool sanitize_movement_children(nlohmann::json& movement_entry, const std::vector<int>& remap) {
    bool changed = false;
    if (movement_entry.is_array()) {
        if (auto* child_array = locate_child_array(movement_entry)) {
            changed |= sanitize_child_entries(*child_array, remap);
        }
    } else if (movement_entry.is_object()) {
        auto it = movement_entry.find("children");
        if (it != movement_entry.end()) {
            changed |= sanitize_child_entries(*it, remap);
        }
    }
    return changed;
}

bool ensure_child_entries(nlohmann::json& movement_entry, std::size_t child_count) {
    if (child_count == 0) {
        return false;
    }

    bool changed = false;
    nlohmann::json* child_array = nullptr;

    if (movement_entry.is_array()) {
        child_array = locate_child_array(movement_entry);
        if (!child_array) {
            movement_entry.push_back(nlohmann::json::array());
            child_array = &movement_entry.back();
            changed = true;
        }
    } else if (movement_entry.is_object()) {
        auto it = movement_entry.find("children");
        if (it == movement_entry.end() || !it->is_array()) {
            movement_entry["children"] = nlohmann::json::array();
            child_array = &movement_entry["children"];
            changed = true;
        } else {
            child_array = &(*it);
        }
    } else {
        return changed;
    }

    if (!child_array || !child_array->is_array()) {
        return changed;
    }

    std::vector<bool> present(child_count, false);
    for (auto& entry : child_array->get_ref<nlohmann::json::array_t&>()) {
        int idx = -1;
        if (!extract_child_index(entry, idx) || idx < 0 || static_cast<std::size_t>(idx) >= child_count) {
            continue;
        }
        const std::size_t slot = static_cast<std::size_t>(idx);
        present[slot] = true;

        if (entry.is_array()) {
            auto ensure_index = [&](std::size_t i, nlohmann::json value) {
                if (entry.size() <= i) {
                    entry.push_back(std::move(value));
                    changed = true;
                }
            };
            ensure_index(0, idx);
            ensure_index(1, 0);
            ensure_index(2, 0);
            ensure_index(3, 0.0);
            ensure_index(4, true);
            ensure_index(5, true);
            if (!entry[4].is_boolean()) {
                entry[4] = true;
                changed = true;
            }
            if (!entry[5].is_boolean()) {
                entry[5] = true;
                changed = true;
            }
        } else if (entry.is_object()) {
            if (!entry.contains("child_index") || !entry["child_index"].is_number_integer() || entry["child_index"].get<int>() != idx) {
                entry["child_index"] = idx;
                changed = true;
            }
            if (!entry.contains("visible") || !entry["visible"].is_boolean()) {
                entry["visible"] = true;
                changed = true;
            }
            if (!entry.contains("render_in_front") || !entry["render_in_front"].is_boolean()) {
                entry["render_in_front"] = true;
                changed = true;
            }
            if (!entry.contains("dx")) {
                entry["dx"] = 0;
                changed = true;
            }
            if (!entry.contains("dy")) {
                entry["dy"] = 0;
                changed = true;
            }
            if (!entry.contains("degree") && !entry.contains("rotation")) {
                entry["degree"] = 0.0;
                changed = true;
            }
        }
    }

    for (std::size_t i = 0; i < present.size(); ++i) {
        if (present[i]) {
            continue;
        }
        child_array->push_back(nlohmann::json::array({static_cast<int>(i), 0, 0, 0.0, true, true}));
        changed = true;
    }

    return changed;
}

}  // namespace

std::vector<std::string> AnimationDocument::animation_children() const {
    auto* self = const_cast<AnimationDocument*>(this);
    if (!self->base_data_.is_object()) {
        self->base_data_ = nlohmann::json::object();
    }
    auto it = self->base_data_.find("animation_children");
    if (it == self->base_data_.end() || !it->is_array()) {
        nlohmann::json arr = nlohmann::json::array();
        std::unordered_set<std::string> seen;
        for (const auto& [id, payload_dump] : self->animations_) {
            nlohmann::json payload = parse_payload(payload_dump, id);
            auto child_it = payload.find("children");
            if (child_it == payload.end() || !child_it->is_array()) {
                continue;
            }
            for (const auto& entry : *child_it) {
                if (!entry.is_string()) continue;
                std::string name = entry.get<std::string>();
                if (name.empty() || !seen.insert(name).second) continue;
                arr.push_back(std::move(name));
            }
        }
        self->base_data_["animation_children"] = arr;
        it = self->base_data_.find("animation_children");
        self->mark_dirty();
    }
    return parse_child_names(*it);
}

void AnimationDocument::replace_animation_children(const std::vector<std::string>& children) {
    if (!base_data_.is_object()) {
        base_data_ = nlohmann::json::object();
    }
    std::vector<std::string> previous = animation_children();
    std::vector<std::string> sanitized;
    sanitized.reserve(children.size());
    std::unordered_set<std::string> seen;
    for (const auto& entry : children) {
        if (entry.empty()) {
            continue;
        }
        if (seen.insert(entry).second) {
            sanitized.push_back(entry);
        }
    }
    if (previous == sanitized && base_data_.contains("animation_children")) {
        return;
    }
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& name : sanitized) {
        arr.push_back(name);
    }
    base_data_["animation_children"] = std::move(arr);
    const std::vector<int> remap = build_child_index_remap(previous, sanitized);
    (void)rewrite_child_payloads(remap, sanitized);
    mark_dirty();
}

bool AnimationDocument::rewrite_child_payloads(const std::vector<int>& remap,
                                               const std::vector<std::string>& next_children) {
    bool mutated = false;
    for (auto& entry : animations_) {
        const std::string& animation_id = entry.first;
        nlohmann::json payload = parse_payload(entry.second, animation_id);
        bool payload_changed = false;
        if (next_children.empty()) {
            if (payload.contains("children")) {
                payload.erase("children");
                payload_changed = true;
            }
        } else {
            if (!payload.contains("children") || payload["children"] != next_children) {
                payload["children"] = next_children;
                payload_changed = true;
            }
        }

        auto movement_it = payload.find("movement");
        if (movement_it != payload.end() && movement_it->is_array()) {
            for (auto& frame_entry : *movement_it) {
                const bool sanitized = sanitize_movement_children(frame_entry, remap);
                const bool filled = ensure_child_entries(frame_entry, next_children.size());
                payload_changed |= (sanitized || filled);
            }
        }

        if (payload_changed) {
            entry.second = serialize_payload(coerce_payload(animation_id, payload));
            mutated = true;
        }
    }
    return mutated;
}

std::string AnimationDocument::animation_children_signature() const {
    auto names = animation_children();
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& name : names) {
        arr.push_back(name);
    }
    return arr.dump();
}

void AnimationDocument::ensure_document_initialized() {
    bool mutated = false;
    std::vector<std::string> ids;
    ids.reserve(animations_.size());

    for (auto& entry : animations_) {
        nlohmann::json normalized = parse_payload(entry.second, entry.first);
        std::string serialized = serialize_payload(normalized);
        if (serialized != entry.second) {
            entry.second = std::move(serialized);
            mutated = true;
        }
        ids.push_back(entry.first);
    }

    if (ids.empty()) {
        // New document: seed a default animation so the editor/UI start in a valid state.
        nlohmann::json payload = coerce_payload("default", nlohmann::json::object({
                                                           {"source", nlohmann::json{{"kind", "folder"},
                                                                                       {"path", "default"},
                                                                                       {"name", ""}}},
                                                       }));
        animations_["default"] = serialize_payload(payload);
        ids.push_back("default");
        start_animation_ = std::string{"default"};
        mutated = true;
    }

    if (start_animation_ && animations_.count(*start_animation_) == 0) {
        start_animation_.reset();
        mutated = true;
    }

    if (!start_animation_ && !ids.empty()) {
        std::sort(ids.begin(), ids.end());
        auto preferred = std::find(ids.begin(), ids.end(), std::string{"default"});
        start_animation_ = (preferred != ids.end()) ? *preferred : ids.front();
        mutated = true;
    }

    if (mutated) {
        mark_dirty();
    }
}

void AnimationDocument::rebuild_animation_cache() {
    ensure_document_initialized();
}

void AnimationDocument::mark_dirty() const { dirty_ = true; }

double AnimationDocument::scale_percentage() const {
    try {
        if (!base_data_.is_object()) return 100.0;
        const auto it = base_data_.find("size_settings");
        if (it == base_data_.end() || !it->is_object()) return 100.0;
        const auto& ss = *it;
        if (ss.contains("scale_percentage")) {
            const auto& v = ss["scale_percentage"];
            if (v.is_number()) {
                double pct = v.get<double>();
                if (!std::isfinite(pct) || pct <= 0.0) return 100.0;
                return pct;
            }
        }
    } catch (...) {
    }
    return 100.0;
}

}
