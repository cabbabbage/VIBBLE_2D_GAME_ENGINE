#include "AnimationDocument.hpp"

#include <SDL_log.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <nlohmann/json.hpp>

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
        name_value = nullptr;
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
    ensure_bool("loop", false);
    ensure_bool("rnd_start", false);

    int speed_factor = parse_int(payload.contains("speed_factor") ? payload["speed_factor"] : nlohmann::json(1), 1);
    if (speed_factor == 0) speed_factor = 1;
    speed_factor = std::clamp(speed_factor, -20, 20);
    payload["speed_factor"] = speed_factor;

    int frames = parse_int(payload.contains("number_of_frames") ? payload["number_of_frames"] : nlohmann::json(1), 1);
    if (frames < 1) frames = 1;
    payload["number_of_frames"] = frames;

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
    movement[0] = nlohmann::json::array({0, 0});
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

    std::string on_end = "default";
    if (payload.contains("on_end")) {
        if (payload["on_end"].is_string()) {
            on_end = payload["on_end"].get<std::string>();
        } else if (payload["on_end"].is_null()) {
            on_end = "default";
        }
    }
    payload["on_end"] = on_end;

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

void AnimationDocument::save_to_file() const {
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
    mark_dirty();
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

void AnimationDocument::ensure_document_initialized() {
    std::vector<std::string> ids;
    ids.reserve(animations_.size());
    for (auto& entry : animations_) {
        nlohmann::json normalized = parse_payload(entry.second, entry.first);
        entry.second = serialize_payload(normalized);
        ids.push_back(entry.first);
    }
    if (start_animation_ && animations_.count(*start_animation_) == 0) {
        start_animation_.reset();
    }
    if (!start_animation_ && !ids.empty()) {
        std::sort(ids.begin(), ids.end());
        start_animation_ = ids.front();
    }
}

void AnimationDocument::rebuild_animation_cache() {
    ensure_document_initialized();
}

void AnimationDocument::mark_dirty() const { dirty_ = true; }

}

