#include "frame_editor_session.hpp"

#include <cctype>

#include "asset/animation.hpp"

#if defined(FRAME_EDITOR_TEST_PUBLIC_ACCESS)
class DMButton {};
class DMDropdown {};
class DMTextBox {};
class DMCheckbox {};

FrameEditorSession::FrameEditorSession() = default;
FrameEditorSession::~FrameEditorSession() = default;
#endif

void FrameEditorSession::sync_child_frames() {
    if (child_assets_.empty()) {
        for (auto& frame : frames_) {
            frame.children.clear();
        }
        selected_child_index_ = 0;
        return;
    }
    for (auto& frame : frames_) {
        std::vector<ChildFrame> normalized(child_assets_.size());
        for (std::size_t i = 0; i < normalized.size(); ++i) {
            normalized[i].child_index = static_cast<int>(i);
            normalized[i].visible = false;
            normalized[i].render_in_front = true;
            normalized[i].has_data = false;
        }
        for (const auto& existing : frame.children) {
            if (existing.child_index < 0 ||
                existing.child_index >= static_cast<int>(normalized.size())) {
                continue;
            }
            normalized[existing.child_index] = existing;
        }
        frame.children = std::move(normalized);
    }
    if (selected_child_index_ >= static_cast<int>(child_assets_.size())) {
        selected_child_index_ = static_cast<int>(child_assets_.size()) - 1;
    }
    if (selected_child_index_ < 0) {
        selected_child_index_ = 0;
    }
    ensure_child_frames_initialized();
}

void FrameEditorSession::ensure_child_frames_initialized() {
    if (child_assets_.empty()) {
        return;
    }
    const std::size_t child_count = child_assets_.size();
    std::vector<ChildFrame> last_known(child_count);
    std::vector<bool> has_last(child_count, false);
    for (auto& frame : frames_) {
        if (frame.children.size() < child_count) {
            frame.children.resize(child_count);
            for (std::size_t i = 0; i < child_count; ++i) {
                frame.children[i].child_index = static_cast<int>(i);
                frame.children[i].has_data = false;
            }
        }
        for (std::size_t i = 0; i < child_count; ++i) {
            auto& child = frame.children[i];
            child.child_index = static_cast<int>(i);
            if (!child.has_data && has_last[i]) {
                child = last_known[i];
                child.child_index = static_cast<int>(i);
                child.has_data = true;
            } else if (!child.has_data && !has_last[i]) {
                child.visible = false;
                child.render_in_front = true;
                child.dx = 0.0f;
                child.dy = 0.0f;
                child.degree = 0.0f;
            }
            if (child.has_data) {
                last_known[i] = child;
                last_known[i].has_data = true;
                has_last[i] = true;
            }
        }
    }
}

bool FrameEditorSession::timeline_entry_is_static(const nlohmann::json& entry) {
    if (!entry.is_object()) {
        return true;
    }
    auto to_lower = [](const std::string& value) {
        std::string lowered;
        lowered.reserve(value.size());
        for (unsigned char ch : value) {
            lowered.push_back(static_cast<char>(std::tolower(ch)));
        }
        return lowered;
};
    if (entry.contains("mode") && entry["mode"].is_string()) {
        const std::string lowered = to_lower(entry["mode"].get<std::string>());
        if (lowered == "async" || lowered == "asynchronous") {
            return false;
        }
    }
    return true;
}

FrameEditorSession::ChildFrame FrameEditorSession::child_frame_from_timeline_sample(const nlohmann::json& sample,
                                                                                   int child_index) {
    auto read_int = [](const nlohmann::json& value, int fallback) -> int {
        if (value.is_number_integer()) {
            try {
                return value.get<int>();
            } catch (...) {
            }
        } else if (value.is_number()) {
            try {
                return static_cast<int>(value.get<double>());
            } catch (...) {
            }
        } else if (value.is_string()) {
            try {
                return std::stoi(value.get<std::string>());
            } catch (...) {
            }
        }
        return fallback;
};
    auto read_float = [](const nlohmann::json& value, float fallback) -> float {
        if (value.is_number()) {
            try {
                return static_cast<float>(value.get<double>());
            } catch (...) {
            }
        } else if (value.is_string()) {
            try {
                return std::stof(value.get<std::string>());
            } catch (...) {
            }
        }
        return fallback;
};
    auto read_bool = [](const nlohmann::json& value, bool fallback) -> bool {
        if (value.is_boolean()) {
            return value.get<bool>();
        }
        if (value.is_number_integer()) {
            return value.get<int>() != 0;
        }
        if (value.is_number()) {
            return value.get<double>() != 0.0;
        }
        if (value.is_string()) {
            std::string text = value.get<std::string>();
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            if (text == "true" || text == "1" || text == "yes" || text == "on") return true;
            if (text == "false" || text == "0" || text == "no" || text == "off") return false;
        }
        return fallback;
};

    ChildFrame child;
    child.child_index = child_index;
    child.dx = 0.0f;
    child.dy = 0.0f;
    child.degree = 0.0f;
    child.visible = false;
    child.render_in_front = true;
    child.has_data = false;

    if (sample.is_object()) {
        if (sample.contains("dx")) child.dx = static_cast<float>(read_int(sample["dx"], 0));
        if (sample.contains("dy")) child.dy = static_cast<float>(read_int(sample["dy"], 0));
        if (sample.contains("degree")) {
            child.degree = read_float(sample["degree"], 0.0f);
        } else if (sample.contains("rotation")) {
            child.degree = read_float(sample["rotation"], 0.0f);
        }
        if (sample.contains("visible")) child.visible = read_bool(sample["visible"], child.visible);
        if (sample.contains("render_in_front")) child.render_in_front = read_bool(sample["render_in_front"], child.render_in_front);
        child.has_data = true;
    } else if (sample.is_array()) {
        if (!sample.empty()) child.dx = static_cast<float>(read_int(sample[0], 0));
        if (sample.size() > 1) child.dy = static_cast<float>(read_int(sample[1], 0));
        if (sample.size() > 2) child.degree = read_float(sample[2], 0.0f);
        if (sample.size() > 3) child.visible = read_bool(sample[3], child.visible);
        if (sample.size() > 4) child.render_in_front = read_bool(sample[4], child.render_in_front);
        child.has_data = true;
    }
    return child;
}

nlohmann::json FrameEditorSession::child_frame_to_json(const ChildFrame& frame) {
    const bool has_sample = frame.has_data;
    nlohmann::json sample = nlohmann::json::object();
    sample["dx"] = has_sample ? static_cast<int>(std::lround(frame.dx)) : 0;
    sample["dy"] = has_sample ? static_cast<int>(std::lround(frame.dy)) : 0;
    sample["degree"] = has_sample ? static_cast<double>(frame.degree) : 0.0;
    sample["visible"] = has_sample ? frame.visible : false;
    sample["render_in_front"] = has_sample ? frame.render_in_front : true;
    return sample;
}

void FrameEditorSession::apply_child_timelines_from_payload(const nlohmann::json& payload) {
    if (!payload.is_object()) {
        return;
    }
    if (frames_.empty() || child_assets_.empty()) {
        return;
    }
    auto timelines_it = payload.find("child_timelines");
    if (timelines_it == payload.end() || !timelines_it->is_array()) {
        return;
    }
    ensure_child_mode_size();
    std::unordered_map<std::string, int> index_by_name;
    index_by_name.reserve(child_assets_.size());
    for (std::size_t i = 0; i < child_assets_.size(); ++i) {
        index_by_name.emplace(child_assets_[i], static_cast<int>(i));
    }
    for (const auto& entry : *timelines_it) {
        if (!entry.is_object()) {
            continue;
        }
        int child_index = -1;
        if (entry.contains("child") && entry["child"].is_number_integer()) {
            child_index = entry["child"].get<int>();
        } else if (entry.contains("child_index") && entry["child_index"].is_number_integer()) {
            child_index = entry["child_index"].get<int>();
        }
        if ((child_index < 0 || child_index >= static_cast<int>(child_assets_.size())) && entry.contains("asset") && entry["asset"].is_string()) {
            auto lookup = index_by_name.find(entry["asset"].get<std::string>());
            if (lookup != index_by_name.end()) {
                child_index = lookup->second;
            }
        }
        if (child_index < 0 || child_index >= static_cast<int>(child_assets_.size())) {
            continue;
        }
        const bool is_static = timeline_entry_is_static(entry);
        child_modes_[static_cast<std::size_t>(child_index)] = is_static ? AnimationChildMode::Static : AnimationChildMode::Async;
        if (!is_static) {
            continue;
        }
        auto frames_it = entry.find("frames");
        if (frames_it == entry.end() || !frames_it->is_array()) {
            continue;
        }
        const auto& samples = *frames_it;
        for (std::size_t frame_idx = 0; frame_idx < frames_.size(); ++frame_idx) {
            if (child_index >= static_cast<int>(frames_[frame_idx].children.size())) {
                continue;
            }
            ChildFrame sample = (frame_idx < samples.size()) ? child_frame_from_timeline_sample(samples[frame_idx], child_index) : child_frame_from_timeline_sample(nlohmann::json::object(), child_index);
            frames_[frame_idx].children[child_index] = sample;
        }
    }
}

nlohmann::json FrameEditorSession::build_child_timelines_payload(const nlohmann::json& existing_payload) const {
    nlohmann::json normalized = nlohmann::json::array();
    if (child_assets_.empty()) {
        return normalized;
    }
    ensure_child_mode_size();
    std::unordered_map<std::string, nlohmann::json> by_asset;
    auto it = existing_payload.find("child_timelines");
    if (it != existing_payload.end() && it->is_array()) {
        for (const auto& entry : *it) {
            if (!entry.is_object()) {
                continue;
            }
            std::string asset = entry.value("asset", std::string{});
            if (asset.empty()) {
                int idx = entry.value("child", entry.value("child_index", -1));
                if (idx >= 0 && static_cast<std::size_t>(idx) < child_assets_.size()) {
                    asset = child_assets_[static_cast<std::size_t>(idx)];
                }
            }
            if (asset.empty()) {
                continue;
            }
            if (by_asset.find(asset) == by_asset.end()) {
                by_asset.emplace(asset, entry);
            }
        }
    }

    normalized.get_ref<nlohmann::json::array_t&>().reserve(child_assets_.size());
    for (std::size_t child_idx = 0; child_idx < child_assets_.size(); ++child_idx) {
        const std::string& asset_name = child_assets_[child_idx];
        nlohmann::json entry = nlohmann::json::object();
        auto existing = by_asset.find(asset_name);
        if (existing != by_asset.end()) {
            entry = existing->second;
        }
        entry["child"] = static_cast<int>(child_idx);
        entry["child_index"] = static_cast<int>(child_idx);
        entry["asset"] = asset_name;
        if (!entry.contains("animation") || !entry["animation"].is_string()) {
            entry["animation"] = std::string{};
        }
        const bool is_static = child_mode(static_cast<int>(child_idx)) != AnimationChildMode::Async;
        entry["mode"] = is_static ? "static" : "async";
        if (is_static) {
            nlohmann::json frames = nlohmann::json::array();
            frames.get_ref<nlohmann::json::array_t&>().reserve(frames_.size());
            for (const auto& movement_frame : frames_) {
                ChildFrame sample{};
                if (child_idx < movement_frame.children.size()) {
                    sample = movement_frame.children[child_idx];
                }
                sample.child_index = static_cast<int>(child_idx);
                frames.push_back(child_frame_to_json(sample));
            }
            if (frames.empty()) {
                ChildFrame sample{};
                sample.child_index = static_cast<int>(child_idx);
                sample.visible = false;
                sample.render_in_front = true;
                frames.push_back(child_frame_to_json(sample));
            }
            entry["frames"] = std::move(frames);
        } else if (!entry.contains("frames") || !entry["frames"].is_array()) {
            entry["frames"] = nlohmann::json::array();
        }
        normalized.push_back(std::move(entry));
    }
    return normalized;
}

void FrameEditorSession::ensure_child_mode_size() const {
    const std::size_t desired = child_assets_.size();
    if (child_modes_.size() == desired) return;
    std::vector<AnimationChildMode> next(desired, AnimationChildMode::Static);
    const std::size_t copy_count = std::min(desired, child_modes_.size());
    for (std::size_t i = 0; i < copy_count; ++i) {
        next[i] = child_modes_[i];
    }
    child_modes_ = std::move(next);
}

AnimationChildMode FrameEditorSession::child_mode(int child_index) const {
    if (child_index < 0 || static_cast<std::size_t>(child_index) >= child_modes_.size()) {
        return AnimationChildMode::Static;
    }
    return child_modes_[static_cast<std::size_t>(child_index)];
}

int FrameEditorSession::child_mode_index(AnimationChildMode mode) const {
    return (mode == AnimationChildMode::Async) ? 1 : 0;
}
