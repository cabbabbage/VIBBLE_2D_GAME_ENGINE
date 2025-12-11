#include "FrameChildrenEditor.hpp"

#include <SDL_image.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>

#include "../../AnimationDocument.hpp"
#include "../FrameToolsPanel.hpp"
#include "../movement/MovementCanvas.hpp"
#include "../../../../dm_styles.hpp"
#include "../../../../draw_utils.hpp"
#include "../../../../../render/scaling_logic.hpp"

namespace fs = std::filesystem;

namespace animation_editor {
namespace {

constexpr int kMarkerRadius = 6;

SDL_Point round_point(SDL_FPoint p) {
    return SDL_Point{static_cast<int>(std::lround(p.x)), static_cast<int>(std::lround(p.y))};
}

void render_child_label(SDL_Renderer* renderer, const std::string& text, int x, int y) {
    if (!renderer || text.empty()) return;
    const DMLabelStyle& style = DMStyles::Label();
    TTF_Font* font = style.open_font();
    if (!font) return;
    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text.c_str(), style.color);
    if (!surf) {
        TTF_CloseFont(font);
        return;
    }
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    if (tex) {
        SDL_Rect dst{x, y, surf->w, surf->h};
        SDL_RenderCopy(renderer, tex, nullptr, &dst);
        SDL_DestroyTexture(tex);
    }
    SDL_FreeSurface(surf);
    TTF_CloseFont(font);
}

bool is_true(const nlohmann::json& value, bool fallback) {
    if (value.is_boolean()) return value.get<bool>();
    if (value.is_number_integer()) return value.get<int>() != 0;
    return fallback;
}

bool has_numeric_stem(const fs::path& path) {
    std::string stem = path.stem().string();
    if (stem.empty()) return false;
    for (char ch : stem) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            return false;
        }
    }
    return true;
}

bool iequals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

std::string lowercase_copy(std::string value) {
    std::transform(value.begin(),
                   value.end(),
                   value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

}

FrameChildrenEditor::FrameChildrenEditor() = default;

void FrameChildrenEditor::set_document(std::shared_ptr<AnimationDocument> document) {
    if (document_ == document) {
        return;
    }
    document_ = std::move(document);
    payload_signature_.clear();
    payload_cache_.clear();
    children_signature_cache_.clear();
    invalidate_child_caches();
    reload_from_document();
}

void FrameChildrenEditor::set_animation_id(const std::string& animation_id) {
    if (animation_id_ == animation_id) {
        return;
    }
    animation_id_ = animation_id;
    payload_signature_.clear();
    payload_cache_.clear();
    children_signature_cache_.clear();
    invalidate_child_caches();
    reload_from_document();
}

void FrameChildrenEditor::set_preview_provider(std::shared_ptr<PreviewProvider> provider) {
    preview_ = std::move(provider);
}

void FrameChildrenEditor::set_tools_panel(FrameToolsPanel* panel) {
    tools_panel_ = panel;
    if (tools_panel_) {
        tools_panel_->set_children_callbacks(
            [this](int index) { this->select_child(index); },
            [this]() { this->apply_current_to_next(); },
            [this](bool visible) { this->set_child_visible(visible); },
            [this](int mode_index) {
                this->set_child_mode(mode_index == 0 ? AnimationChildMode::Static : AnimationChildMode::Async);
            },
            [this](const std::string& name) { this->add_or_rename_child(name); },
            [this]() { this->remove_selected_child(); });
    }
    refresh_tools_panel();
}

void FrameChildrenEditor::set_canvas(MovementCanvas* canvas) {
    canvas_ = canvas;
}

void FrameChildrenEditor::set_selected_frame(int index) {
    if (frames_.empty()) {
        selected_frame_index_ = 0;
        return;
    }
    index = std::clamp(index, 0, static_cast<int>(frames_.size()) - 1);
    if (selected_frame_index_ == index) {
        return;
    }
    selected_frame_index_ = index;
    refresh_tools_panel();
}

std::string FrameChildrenEditor::selected_child_id() const {
    if (selected_child_index_ < 0 || selected_child_index_ >= static_cast<int>(child_ids_.size())) {
        return std::string{};
    }
    return child_ids_[static_cast<std::size_t>(selected_child_index_)];
}

AnimationChildMode FrameChildrenEditor::selected_child_mode() const {
    if (child_ids_.empty()) {
        return AnimationChildMode::Static;
    }
    return child_mode(selected_child_index_);
}

void FrameChildrenEditor::refresh_payload_cache_from_document() {
    if (!document_ || animation_id_.empty()) {
        payload_signature_.clear();
        payload_cache_.clear();
        children_signature_cache_.clear();
        reload_from_document();
        return;
    }
    auto payload_dump = document_->animation_payload(animation_id_);
    payload_cache_ = payload_dump.has_value() ? *payload_dump : std::string{};
    children_signature_cache_ = document_->animation_children_signature();
    payload_signature_.clear();
    payload_signature_ = payload_cache_;
    if (!children_signature_cache_.empty()) {
        payload_signature_ += "|" + children_signature_cache_;
    }
    reload_from_document();
}

void FrameChildrenEditor::update() {
    if (!document_ || animation_id_.empty()) {
        return;
    }
    auto payload_dump = document_->animation_payload(animation_id_);
    std::string payload = payload_dump.has_value() ? *payload_dump : std::string{};
    std::string children_sig = document_->animation_children_signature();
    std::string signature = payload;
    signature += "|" + children_sig;
    if (payload_signature_ != signature) {
        payload_signature_ = signature;
        payload_cache_ = std::move(payload);
        children_signature_cache_ = std::move(children_sig);
        reload_from_document();
    }
    refresh_tools_panel();
}

void FrameChildrenEditor::render(SDL_Renderer* renderer) const {
    if (!renderer || !canvas_ || child_ids_.empty()) {
        return;
    }
    const MovementFrame* frame = current_frame();
    if (!frame) {
        return;
    }
    float parent_scale = canvas_pixels_per_unit();
    if (!std::isfinite(parent_scale) || parent_scale <= 0.0f) {
        parent_scale = 1.0f;
    }
    float doc_scale = document_scale_factor();
    if (!std::isfinite(doc_scale) || doc_scale <= 0.0f) {
        doc_scale = 1.0f;
    }
    const float offset_scale = parent_scale;
    const float sprite_scale_base = parent_scale * doc_scale;
    SDL_FPoint anchor_screen = canvas_ ? canvas_->frame_anchor_screen(selected_frame_index_) : world_to_screen(frame_anchor(selected_frame_index_));

    for (std::size_t i = 0; i < child_ids_.size() && i < frame->children.size(); ++i) {
        const auto& child = frame->children[i];
        if (!child.visible) {
            continue;
        }
        int tex_w = 0;
        int tex_h = 0;
        SDL_Texture* texture = acquire_child_texture(renderer, child_ids_[i], &tex_w, &tex_h);
        if (!texture || tex_w <= 0 || tex_h <= 0) {
            continue;
        }
        if (!std::isfinite(sprite_scale_base) || sprite_scale_base <= 0.0f) {
            continue;
        }
        SDL_FPoint screen = child_screen_position(child, anchor_screen, offset_scale);
        const float dst_w = sprite_scale_base * static_cast<float>(tex_w);
        const float dst_h = sprite_scale_base * static_cast<float>(tex_h);
        if (!(std::isfinite(dst_w) && std::isfinite(dst_h)) || dst_w <= 0.0f || dst_h <= 0.0f) {
            continue;
        }
        SDL_Rect dst{static_cast<int>(std::round(screen.x - dst_w * 0.5f)),
                     static_cast<int>(std::round(screen.y - dst_h)), static_cast<int>(std::round(dst_w)), static_cast<int>(std::round(dst_h))};
        if (dst.w <= 0 || dst.h <= 0) {
            continue;
        }
        SDL_Point pivot{dst.w / 2, dst.h};
        SDL_RenderCopyEx(renderer, texture, nullptr, &dst, child.rotation, &pivot, SDL_FLIP_NONE);
    }

    for (std::size_t i = 0; i < child_ids_.size() && i < frame->children.size(); ++i) {
        const auto& child = frame->children[i];
        SDL_FPoint screen = child_screen_position(child, anchor_screen, offset_scale);
        SDL_Point center = round_point(screen);
        const bool is_selected = static_cast<int>(i) == selected_child_index_;
        const int radius = is_selected ? kMarkerRadius + 1 : kMarkerRadius - 1;
        SDL_Rect marker{center.x - radius, center.y - radius, radius * 2, radius * 2};

        SDL_Color base = is_selected ? DMStyles::AccentButton().hover_bg : DMStyles::HeaderButton().bg;
        Uint8 alpha = child.visible ? 230 : 90;
        SDL_SetRenderDrawColor(renderer, base.r, base.g, base.b, alpha);
        SDL_RenderFillRect(renderer, &marker);
        SDL_SetRenderDrawColor(renderer, DMStyles::Border().r, DMStyles::Border().g, DMStyles::Border().b, 255);
        SDL_RenderDrawRect(renderer, &marker);
        render_child_label(renderer, child_ids_[i], marker.x + marker.w + 4, marker.y - 4);
    }
}

bool FrameChildrenEditor::handle_event(const SDL_Event& e) {
    if (!canvas_ || child_ids_.empty()) {
        return false;
    }
    switch (e.type) {
        case SDL_MOUSEBUTTONDOWN: {
            if (e.button.button != SDL_BUTTON_LEFT) {
                break;
            }
            if (!point_in_canvas(e.button.x, e.button.y)) {
                break;
            }
            const int hit = hit_test_child(e.button.x, e.button.y);
            if (hit >= 0) {
                select_child(hit);
                dragging_child_ = true;
                drag_start_screen_ = SDL_Point{e.button.x, e.button.y};
                if (auto* child = current_child()) {
                    drag_snapshot_ = *child;
                }
                return true;
            }
            break;
        }
        case SDL_MOUSEMOTION: {
            if (!dragging_child_) {
                break;
            }
            SDL_Point screen{e.motion.x, e.motion.y};
            float parent_scale = canvas_pixels_per_unit();
            if (!std::isfinite(parent_scale) || parent_scale <= 0.0f) {
                parent_scale = 1.0f;
            }
            const float offset_scale = parent_scale;
            SDL_FPoint anchor_screen = canvas_ ? canvas_->frame_anchor_screen(selected_frame_index_) : world_to_screen(frame_anchor(selected_frame_index_));
            if (auto* child = current_child()) {
                float denom = (std::isfinite(offset_scale) && offset_scale > 0.0f) ? offset_scale : 1.0f;
                child->dx = static_cast<float>(std::round((static_cast<float>(screen.x) - anchor_screen.x) / denom));
                child->dy = static_cast<float>(std::round((static_cast<float>(screen.y) - anchor_screen.y) / denom));
                persist_changes();
                refresh_tools_panel();
            }
            return true;
        }
        case SDL_MOUSEBUTTONUP: {
            if (dragging_child_ && e.button.button == SDL_BUTTON_LEFT) {
                dragging_child_ = false;
                return true;
            }
            break;
        }
        default:
            break;
    }
    return false;
}

bool FrameChildrenEditor::handle_key_event(const SDL_Event& e) {
    if (e.type != SDL_KEYDOWN) {
        return false;
    }
    if (child_ids_.empty()) {
        return false;
    }
    if (e.key.keysym.sym != SDLK_LEFT && e.key.keysym.sym != SDLK_RIGHT) {
        return false;
    }
    ChildFrame* child = current_child();
    if (!child) {
        return false;
    }
    float delta = (e.key.keysym.sym == SDLK_LEFT) ? -1.0f : 1.0f;
    if (e.key.keysym.mod & KMOD_SHIFT) {
        delta *= 10.0f;
    }
    child->rotation = std::round((child->rotation + delta) * 10.0f) / 10.0f;
    persist_changes();
    refresh_tools_panel();
    return true;
}

void FrameChildrenEditor::reload_from_document() {
    std::vector<std::string> previous_children = child_ids_;
    frames_.clear();
    child_ids_.clear();
    selected_child_index_ = 0;
    if (document_) {
        child_ids_ = document_->animation_children();
        if (children_signature_cache_.empty()) {
            children_signature_cache_ = document_->animation_children_signature();
        }
    }

    if (payload_cache_.empty() && document_ && !animation_id_.empty()) {
        auto payload_dump = document_->animation_payload(animation_id_);
        payload_cache_ = payload_dump.has_value() ? *payload_dump : std::string{};
    }

    if (payload_signature_.empty()) {
        payload_signature_ = payload_cache_;
        if (!children_signature_cache_.empty()) {
            payload_signature_ += "|" + children_signature_cache_;
        }
    }

    if (payload_cache_.empty()) {
        frames_.push_back(MovementFrame{});
        refresh_tools_panel();
        return;
    }

    nlohmann::json payload = nlohmann::json::object();
    {
        nlohmann::json parsed = nlohmann::json::parse(payload_cache_, nullptr, false);
        if (parsed.is_object()) {
            payload = parsed;
        }
    }

    nlohmann::json movement = nlohmann::json::array();
    if (payload.contains("movement")) {
        movement = payload["movement"];
    }

    if (!movement.is_array() || movement.empty()) {
        frames_.push_back(MovementFrame{});
    } else {
        for (const auto& entry : movement) {
            MovementFrame frame;
            if (entry.is_array()) {
                if (!entry.empty() && entry[0].is_number()) frame.dx = static_cast<float>(entry[0].get<double>());
                if (entry.size() > 1 && entry[1].is_number()) frame.dy = static_cast<float>(entry[1].get<double>());
                if (entry.size() > 2 && entry[2].is_boolean()) frame.resort_z = entry[2].get<bool>();
                auto find_children_array = [](const nlohmann::json& arr) -> const nlohmann::json* {
                    if (!arr.is_array()) {
                        return nullptr;
                    }
                    for (std::size_t idx = 2; idx < arr.size(); ++idx) {
                        const auto& candidate = arr[idx];
                        if (!candidate.is_array()) {
                            continue;
                        }
                        if (candidate.empty()) {
                            return &candidate;
                        }
                        const auto& first = candidate.front();
                        if (first.is_array() || first.is_object()) {
                            return &candidate;
                        }
                    }
                    return nullptr;
};
                if (const nlohmann::json* children = find_children_array(entry)) {
                    for (const auto& child_entry : *children) {
                        if (!child_entry.is_array() || child_entry.empty()) continue;
                        ChildFrame child;
                        try { child.child_index = child_entry[0].get<int>(); } catch (...) { child.child_index = -1; }
                        if (child_entry.size() > 1 && child_entry[1].is_number()) {
                            child.dx = static_cast<float>(child_entry[1].get<double>());
                        }
                        if (child_entry.size() > 2 && child_entry[2].is_number()) {
                            child.dy = static_cast<float>(child_entry[2].get<double>());
                        }
                        if (child_entry.size() > 3 && child_entry[3].is_number()) {
                            child.rotation = static_cast<float>(child_entry[3].get<double>());
                        }
                        if (child_entry.size() > 4) {
                            child.visible = is_true(child_entry[4], true);
                        }
                        if (child_entry.size() > 5) {
                            child.render_in_front = is_true(child_entry[5], true);
                        }
                        frame.children.push_back(child);
                    }
                }
            } else if (entry.is_object()) {
                frame.dx = static_cast<float>(entry.value("dx", 0.0));
                frame.dy = static_cast<float>(entry.value("dy", 0.0));
                frame.resort_z = entry.value("resort_z", false);
                if (entry.contains("children") && entry["children"].is_array()) {
                    for (const auto& child_entry : entry["children"]) {
                        if (!child_entry.is_object() && !child_entry.is_array()) continue;
                        ChildFrame child;
                        if (child_entry.is_object()) {
                            child.child_index = child_entry.value("child_index", -1);
                            child.dx = static_cast<float>(child_entry.value("dx", 0.0));
                            child.dy = static_cast<float>(child_entry.value("dy", 0.0));
                            double deg = child_entry.value("degree", child_entry.value("rotation", 0.0));
                            child.rotation = static_cast<float>(deg);
                            child.visible = child_entry.value("visible", true);
                            child.render_in_front = child_entry.value("render_in_front", true);
                        } else if (child_entry.is_array()) {
                            try { child.child_index = child_entry[0].get<int>(); } catch (...) { child.child_index = -1; }
                            if (child_entry.size() > 1 && child_entry[1].is_number()) {
                                child.dx = static_cast<float>(child_entry[1].get<double>());
                            }
                            if (child_entry.size() > 2 && child_entry[2].is_number()) {
                                child.dy = static_cast<float>(child_entry[2].get<double>());
                            }
                            if (child_entry.size() > 3 && child_entry[3].is_number()) {
                                child.rotation = static_cast<float>(child_entry[3].get<double>());
                            }
                            if (child_entry.size() > 4) {
                                child.visible = is_true(child_entry[4], true);
                            }
                            if (child_entry.size() > 5) {
                                child.render_in_front = is_true(child_entry[5], true);
                            }
                        }
                        frame.children.push_back(child);
                    }
                }
            }
            frames_.push_back(frame);
        }
    }

    if (frames_.empty()) {
        frames_.push_back(MovementFrame{});
    }
    frames_.front().dx = 0.0f;
    frames_.front().dy = 0.0f;

    ensure_child_vectors();
    apply_child_timelines_from_payload(payload);
    if (child_ids_ != previous_children) {
        child_asset_dir_cache_.clear();
        child_previews_.clear();
    }
    selected_frame_index_ = std::clamp(selected_frame_index_, 0, static_cast<int>(frames_.size()) - 1);

    refresh_tools_panel();
}

void FrameChildrenEditor::ensure_child_vectors() {
    if (child_ids_.empty()) {
        for (auto& frame : frames_) {
            frame.children.clear();
        }
        selected_child_index_ = 0;
        child_modes_.clear();
        return;
    }
    ensure_child_mode_size();
    for (auto& frame : frames_) {
        std::vector<ChildFrame> normalized(child_ids_.size());
        for (std::size_t i = 0; i < normalized.size(); ++i) {
            normalized[i].child_index = static_cast<int>(i);
            normalized[i].visible = false;
            normalized[i].render_in_front = true;
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
    if (selected_child_index_ >= static_cast<int>(child_ids_.size())) {
        selected_child_index_ = static_cast<int>(child_ids_.size()) - 1;
    }
    if (selected_child_index_ < 0) {
        selected_child_index_ = 0;
    }
}

void FrameChildrenEditor::ensure_child_mode_size() {
    const std::size_t desired = child_ids_.size();
    if (child_modes_.size() == desired) return;
    std::vector<AnimationChildMode> next(desired, AnimationChildMode::Static);
    const std::size_t copy_count = std::min(desired, child_modes_.size());
    for (std::size_t i = 0; i < copy_count; ++i) {
        next[i] = child_modes_[i];
    }
    child_modes_ = std::move(next);
}

AnimationChildMode FrameChildrenEditor::child_mode(int child_index) const {
    if (child_index < 0 || static_cast<std::size_t>(child_index) >= child_modes_.size()) {
        return AnimationChildMode::Static;
    }
    return child_modes_[static_cast<std::size_t>(child_index)];
}

int FrameChildrenEditor::child_mode_index(AnimationChildMode mode) const {
    return (mode == AnimationChildMode::Async) ? 1 : 0;
}

std::vector<int> FrameChildrenEditor::build_child_index_remap(const std::vector<std::string>& previous,
                                                             const std::vector<std::string>& next) const {
    std::vector<int> remap(previous.size(), -1);
    if (previous.empty() || next.empty()) {
        return remap;
    }
    std::unordered_map<std::string, int> next_lookup;
    next_lookup.reserve(next.size());
    for (std::size_t i = 0; i < next.size(); ++i) {
        next_lookup[next[i]] = static_cast<int>(i);
    }
    for (std::size_t i = 0; i < previous.size(); ++i) {
        auto it = next_lookup.find(previous[i]);
        if (it != next_lookup.end()) {
            remap[i] = it->second;
        }
    }
    return remap;
}

void FrameChildrenEditor::remap_child_indices(const std::vector<int>& remap) {
    if (remap.empty() || frames_.empty()) {
        return;
    }
    const std::size_t next_count = child_ids_.size();
    for (auto& frame : frames_) {
        std::vector<ChildFrame> next(next_count);
        for (std::size_t i = 0; i < next_count; ++i) {
            next[i].child_index = static_cast<int>(i);
            next[i].visible = false;
            next[i].render_in_front = true;
        }
        for (std::size_t i = 0; i < remap.size(); ++i) {
            const int to = remap[i];
            if (to < 0 || static_cast<std::size_t>(to) >= next.size()) {
                continue;
            }
            if (i >= frame.children.size()) {
                continue;
            }
            next[static_cast<std::size_t>(to)] = frame.children[i];
            next[static_cast<std::size_t>(to)].child_index = to;
        }
        frame.children = std::move(next);
    }
}

bool FrameChildrenEditor::timeline_entry_is_static(const nlohmann::json& entry) const {
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

FrameChildrenEditor::ChildFrame FrameChildrenEditor::child_frame_from_sample(const nlohmann::json& sample,
                                                                             int child_index) const {
    auto read_int = [](const nlohmann::json& value, int fallback) -> int {
        if (value.is_number_integer()) {
            try {
                return value.get<int>();
            } catch (...) {}
        } else if (value.is_number()) {
            try {
                return static_cast<int>(value.get<double>());
            } catch (...) {}
        } else if (value.is_string()) {
            try {
                return std::stoi(value.get<std::string>());
            } catch (...) {}
        }
        return fallback;
};
    auto read_float = [](const nlohmann::json& value, float fallback) -> float {
        if (value.is_number()) {
            try {
                return static_cast<float>(value.get<double>());
            } catch (...) {}
        } else if (value.is_string()) {
            try {
                return std::stof(value.get<std::string>());
            } catch (...) {}
        }
        return fallback;
};
    auto read_bool = [](const nlohmann::json& value, bool fallback) -> bool {
        if (value.is_boolean()) return value.get<bool>();
        if (value.is_number_integer()) return value.get<int>() != 0;
        if (value.is_number()) return value.get<double>() != 0.0;
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
    child.rotation = 0.0f;
    child.visible = false;
    child.render_in_front = true;

    if (sample.is_object()) {
        if (sample.contains("dx")) child.dx = static_cast<float>(read_int(sample["dx"], 0));
        if (sample.contains("dy")) child.dy = static_cast<float>(read_int(sample["dy"], 0));
        if (sample.contains("degree")) {
            child.rotation = read_float(sample["degree"], 0.0f);
        } else if (sample.contains("rotation")) {
            child.rotation = read_float(sample["rotation"], 0.0f);
        }
        if (sample.contains("visible")) child.visible = read_bool(sample["visible"], child.visible);
        if (sample.contains("render_in_front")) child.render_in_front = read_bool(sample["render_in_front"], child.render_in_front);
    } else if (sample.is_array()) {
        if (!sample.empty()) child.dx = static_cast<float>(read_int(sample[0], 0));
        if (sample.size() > 1) child.dy = static_cast<float>(read_int(sample[1], 0));
        if (sample.size() > 2) child.rotation = read_float(sample[2], 0.0f);
        if (sample.size() > 3) child.visible = read_bool(sample[3], child.visible);
        if (sample.size() > 4) child.render_in_front = read_bool(sample[4], child.render_in_front);
    }
    return child;
}

nlohmann::json FrameChildrenEditor::child_frame_to_json(const ChildFrame& frame) const {
    nlohmann::json sample = nlohmann::json::object();
    sample["dx"] = static_cast<int>(std::lround(frame.dx));
    sample["dy"] = static_cast<int>(std::lround(frame.dy));
    sample["degree"] = static_cast<double>(frame.rotation);
    sample["visible"] = frame.visible;
    sample["render_in_front"] = frame.render_in_front;
    return sample;
}

void FrameChildrenEditor::apply_child_timelines_from_payload(const nlohmann::json& payload) {
    if (!payload.is_object()) {
        return;
    }
    if (frames_.empty() || child_ids_.empty()) {
        return;
    }
    auto timelines_it = payload.find("child_timelines");
    if (timelines_it == payload.end() || !timelines_it->is_array()) {
        return;
    }
    ensure_child_mode_size();
    std::unordered_map<std::string, int> index_by_name;
    index_by_name.reserve(child_ids_.size());
    for (std::size_t i = 0; i < child_ids_.size(); ++i) {
        index_by_name.emplace(child_ids_[i], static_cast<int>(i));
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
        if ((child_index < 0 || child_index >= static_cast<int>(child_ids_.size())) && entry.contains("asset") && entry["asset"].is_string()) {
            auto lookup = index_by_name.find(entry["asset"].get<std::string>());
            if (lookup != index_by_name.end()) {
                child_index = lookup->second;
            }
        }
        if (child_index < 0 || child_index >= static_cast<int>(child_ids_.size())) {
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
            ChildFrame sample = (frame_idx < samples.size()) ? child_frame_from_sample(samples[frame_idx], child_index) : child_frame_from_sample(nlohmann::json::object(), child_index);
            frames_[frame_idx].children[child_index] = sample;
        }
    }
}

nlohmann::json FrameChildrenEditor::build_child_timelines_payload(const nlohmann::json& existing_payload) {
    nlohmann::json normalized = nlohmann::json::array();
    if (child_ids_.empty()) {
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
                if (idx >= 0 && static_cast<std::size_t>(idx) < child_ids_.size()) {
                    asset = child_ids_[static_cast<std::size_t>(idx)];
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

    normalized.get_ref<nlohmann::json::array_t&>().reserve(child_ids_.size());
    for (std::size_t child_idx = 0; child_idx < child_ids_.size(); ++child_idx) {
        const std::string& asset_name = child_ids_[child_idx];
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

void FrameChildrenEditor::refresh_tools_panel() const {
    if (!tools_panel_) {
        return;
    }
    bool has_children = !child_ids_.empty();
    bool visible = true;
    int mode_index = 0;
    if (const ChildFrame* child = current_child()) {
        visible = child->visible;
    }
    mode_index = child_mode_index(child_mode(selected_child_index_));
    std::string current_name;
    if (selected_child_index_ >= 0 && selected_child_index_ < static_cast<int>(child_ids_.size())) {
        current_name = child_ids_[static_cast<std::size_t>(selected_child_index_)];
    }
    tools_panel_->set_children_state(child_ids_, selected_child_index_, visible, true, mode_index, current_name);
}

void FrameChildrenEditor::select_child(int index) {
    if (child_ids_.empty()) {
        selected_child_index_ = 0;
        refresh_tools_panel();
        return;
    }
    index = std::clamp(index, 0, static_cast<int>(child_ids_.size()) - 1);
    if (selected_child_index_ == index) {
        return;
    }
    selected_child_index_ = index;
    refresh_tools_panel();
}

void FrameChildrenEditor::apply_current_to_next() {
    if (child_ids_.empty()) {
        return;
    }
    if (selected_frame_index_ >= static_cast<int>(frames_.size()) - 1) {
        return;
    }
    const ChildFrame* source = current_child();
    if (!source) {
        return;
    }
    auto& target_frame = frames_[selected_frame_index_ + 1];
    if (selected_child_index_ < 0 ||
        selected_child_index_ >= static_cast<int>(target_frame.children.size())) {
        return;
    }
    target_frame.children[selected_child_index_] = *source;
    target_frame.children[selected_child_index_].child_index = selected_child_index_;
    persist_changes();
}

void FrameChildrenEditor::set_child_visible(bool visible) {
    ChildFrame* child = current_child();
    if (!child) {
        return;
    }
    if (child->visible == visible) {
        return;
    }
    child->visible = visible;
    persist_changes();
}

void FrameChildrenEditor::set_child_mode(AnimationChildMode mode) {
    ensure_child_mode_size();
    if (selected_child_index_ < 0 || static_cast<std::size_t>(selected_child_index_) >= child_modes_.size()) {
        return;
    }
    if (child_modes_[static_cast<std::size_t>(selected_child_index_)] == mode) {
        return;
    }
    child_modes_[static_cast<std::size_t>(selected_child_index_)] = mode;
    persist_changes();
}

void FrameChildrenEditor::add_or_rename_child(const std::string& raw_name) {
    auto trim = [](std::string s) {
        auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
        s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
        return s;
};
    std::string name = trim(raw_name);
    if (name.empty()) {
        return;
    }
    for (const auto& existing : child_ids_) {
        if (existing == name) {
            auto it = std::find(child_ids_.begin(), child_ids_.end(), name);
            if (it != child_ids_.end()) {
                select_child(static_cast<int>(std::distance(child_ids_.begin(), it)));
            }
            return;
        }
    }

    if (selected_child_index_ >= 0 && selected_child_index_ < static_cast<int>(child_ids_.size())) {
        std::vector<std::string> next = child_ids_;
        next[static_cast<std::size_t>(selected_child_index_)] = name;
        apply_child_list_change(next);
    } else {
        std::vector<std::string> next = child_ids_;
        next.push_back(name);
        apply_child_list_change(next);
        select_child(static_cast<int>(next.size()) - 1);
    }
}

void FrameChildrenEditor::remove_selected_child() {
    if (child_ids_.empty()) {
        return;
    }
    if (selected_child_index_ < 0 || selected_child_index_ >= static_cast<int>(child_ids_.size())) {
        return;
    }
    std::vector<std::string> next;
    next.reserve(child_ids_.size() - 1);
    for (std::size_t i = 0; i < child_ids_.size(); ++i) {
        if (static_cast<int>(i) == selected_child_index_) continue;
        next.push_back(child_ids_[i]);
    }
    int next_selection = next.empty() ? 0 : std::clamp(selected_child_index_ - 1, 0, static_cast<int>(next.size()) - 1);
    apply_child_list_change(next);
    select_child(next_selection);
}

void FrameChildrenEditor::apply_child_list_change(const std::vector<std::string>& next_children) {
    const std::vector<std::string> previous = child_ids_;
    const std::vector<int> remap = build_child_index_remap(previous, next_children);
    child_ids_ = next_children;
    std::vector<AnimationChildMode> next_modes(child_ids_.size(), AnimationChildMode::Static);
    for (std::size_t i = 0; i < remap.size(); ++i) {
        const int to = remap[i];
        if (to >= 0 && static_cast<std::size_t>(to) < next_modes.size()) {
            if (i < child_modes_.size()) {
                next_modes[static_cast<std::size_t>(to)] = child_modes_[i];
            }
        }
    }
    child_modes_ = std::move(next_modes);
    remap_child_indices(remap);
    ensure_child_vectors();
    if (child_ids_.empty()) {
        selected_child_index_ = 0;
    } else {
        selected_child_index_ = std::clamp(selected_child_index_, 0, static_cast<int>(child_ids_.size()) - 1);
    }
    refresh_tools_panel();
    persist_changes();
}

void FrameChildrenEditor::persist_changes() {
    if (!document_ || animation_id_.empty()) {
        return;
    }
    nlohmann::json payload = nlohmann::json::object();
    if (!payload_cache_.empty()) {
        nlohmann::json parsed = nlohmann::json::parse(payload_cache_, nullptr, false);
        if (parsed.is_object()) {
            payload = parsed;
        }
    }
    if (document_) {
        document_->replace_animation_children(child_ids_);
        if (child_ids_.empty()) {
            payload.erase("children");
        } else {
            payload["children"] = child_ids_;
        }
    }

    nlohmann::json movement_json = nlohmann::json::array();
    for (std::size_t i = 0; i < frames_.size(); ++i) {
        const auto& frame = frames_[i];
        int dx = static_cast<int>(std::lround(frame.dx));
        int dy = static_cast<int>(std::lround(frame.dy));
        nlohmann::json entry = nlohmann::json::array({dx, dy});
        if (frame.resort_z) {
            entry.push_back(frame.resort_z);
        }
        if (!child_ids_.empty()) {
            while (entry.size() < 4) {
                entry.push_back(nlohmann::json());
            }
            nlohmann::json child_entries = nlohmann::json::array();
            if (!frame.children.empty()) {
                for (const auto& child : frame.children) {
                    if (child.child_index < 0 ||
                        child.child_index >= static_cast<int>(child_ids_.size())) {
                        continue;
                    }
                    nlohmann::json child_json = nlohmann::json::array();
                    child_json.push_back(child.child_index);
                    child_json.push_back(static_cast<int>(std::lround(child.dx)));
                    child_json.push_back(static_cast<int>(std::lround(child.dy)));
                    child_json.push_back(static_cast<double>(child.rotation));
                    child_json.push_back(child.visible);
                    child_json.push_back(child.render_in_front);
                    child_entries.push_back(std::move(child_json));
                }
            }
            entry.push_back(std::move(child_entries));
        }
        movement_json.push_back(std::move(entry));
    }

    if (movement_json.empty()) {
        movement_json.push_back(nlohmann::json::array({0, 0}));
    }
    payload["movement"] = std::move(movement_json);

    int total_dx = 0;
    int total_dy = 0;
    for (std::size_t i = 1; i < frames_.size(); ++i) {
        total_dx += static_cast<int>(std::lround(frames_[i].dx));
        total_dy += static_cast<int>(std::lround(frames_[i].dy));
    }
    payload["movement_total"] = nlohmann::json{{"dx", total_dx}, {"dy", total_dy}};

    ensure_child_mode_size();
    if (child_ids_.empty()) {
        payload.erase("child_timelines");
    } else {
        payload["child_timelines"] = build_child_timelines_payload(payload);
    }

    const std::string updated_payload_dump = payload.dump();
    document_->replace_animation_payload(animation_id_, updated_payload_dump);
    document_->save_to_file();
    auto refreshed = document_->animation_payload(animation_id_);
    payload_cache_ = refreshed.has_value() ? *refreshed : updated_payload_dump;
    children_signature_cache_ = document_ ? document_->animation_children_signature() : std::string{};
    payload_signature_.clear();
    payload_signature_ = payload_cache_;
    if (!children_signature_cache_.empty()) {
        payload_signature_ += "|" + children_signature_cache_;
    }
}

void FrameChildrenEditor::invalidate_child_caches() {
    child_previews_.clear();
    child_asset_dir_cache_.clear();
    cached_assets_root_.clear();
    cached_assets_root_valid_ = false;
    child_scale_cache_.clear();
    manifest_scale_cache_.clear();
    manifest_scale_cache_valid_ = false;
    cached_manifest_path_.clear();
    cached_manifest_path_valid_ = false;
}

FrameChildrenEditor::MovementFrame* FrameChildrenEditor::current_frame() {
    if (frames_.empty()) {
        return nullptr;
    }
    if (selected_frame_index_ < 0 ||
        selected_frame_index_ >= static_cast<int>(frames_.size())) {
        return nullptr;
    }
    return &frames_[selected_frame_index_];
}

const FrameChildrenEditor::MovementFrame* FrameChildrenEditor::current_frame() const {
    if (frames_.empty()) {
        return nullptr;
    }
    if (selected_frame_index_ < 0 ||
        selected_frame_index_ >= static_cast<int>(frames_.size())) {
        return nullptr;
    }
    return &frames_[selected_frame_index_];
}

FrameChildrenEditor::ChildFrame* FrameChildrenEditor::current_child() {
    MovementFrame* frame = current_frame();
    if (!frame || frame->children.empty()) {
        return nullptr;
    }
    if (selected_child_index_ < 0 ||
        selected_child_index_ >= static_cast<int>(frame->children.size())) {
        return nullptr;
    }
    return &frame->children[selected_child_index_];
}

const FrameChildrenEditor::ChildFrame* FrameChildrenEditor::current_child() const {
    const MovementFrame* frame = current_frame();
    if (!frame || frame->children.empty()) {
        return nullptr;
    }
    if (selected_child_index_ < 0 ||
        selected_child_index_ >= static_cast<int>(frame->children.size())) {
        return nullptr;
    }
    return &frame->children[selected_child_index_];
}

SDL_FPoint FrameChildrenEditor::frame_anchor(int frame_index) const {
    if (canvas_) {
        return canvas_->frame_anchor_world(frame_index);
    }
    SDL_FPoint anchor{0.0f, 0.0f};
    if (frames_.empty()) {
        return anchor;
    }
    int idx = std::clamp(frame_index, 0, static_cast<int>(frames_.size()) - 1);
    for (int i = 1; i <= idx; ++i) {
        anchor.x += frames_[i].dx;
        anchor.y += frames_[i].dy;
    }
    return anchor;
}

bool FrameChildrenEditor::point_in_canvas(int x, int y) const {
    if (!canvas_) {
        return false;
    }
    const SDL_Rect& bounds = canvas_->bounds();
    if (bounds.w <= 0 || bounds.h <= 0) {
        return false;
    }
    SDL_Point pt{x, y};
    return SDL_PointInRect(&pt, &bounds) != 0;
}

SDL_FPoint FrameChildrenEditor::screen_to_world(SDL_Point screen) const {
    if (canvas_) {
        return canvas_->screen_to_world(screen);
    }
    return SDL_FPoint{static_cast<float>(screen.x), static_cast<float>(screen.y)};
}

SDL_FPoint FrameChildrenEditor::world_to_screen(const SDL_FPoint& world) const {
    if (canvas_) {
        return canvas_->world_to_screen(world);
    }
    return world;
}

SDL_FPoint FrameChildrenEditor::child_screen_position(const ChildFrame& child,
                                                      const SDL_FPoint& anchor_screen,
                                                      float offset_scale) const {
    float scale = (std::isfinite(offset_scale) && offset_scale > 0.0f) ? offset_scale : 1.0f;
    return SDL_FPoint{anchor_screen.x + child.dx * scale,
                      anchor_screen.y + child.dy * scale};
}

int FrameChildrenEditor::hit_test_child(int x, int y) const {
    const MovementFrame* frame = current_frame();
    if (!frame) {
        return -1;
    }
    float parent_scale = canvas_pixels_per_unit();
    if (!std::isfinite(parent_scale) || parent_scale <= 0.0f) {
        parent_scale = 1.0f;
    }
    const float offset_scale = parent_scale;
    SDL_FPoint anchor_screen = canvas_ ? canvas_->frame_anchor_screen(selected_frame_index_) : world_to_screen(frame_anchor(selected_frame_index_));
    SDL_Point pt{x, y};
    for (std::size_t i = 0; i < child_ids_.size() && i < frame->children.size(); ++i) {
        SDL_FPoint screen = child_screen_position(frame->children[i], anchor_screen, offset_scale);
        SDL_Point center = round_point(screen);
        const bool is_selected = static_cast<int>(i) == selected_child_index_;
        const int radius = is_selected ? kMarkerRadius + 1 : kMarkerRadius - 1;
        SDL_Rect rect{center.x - radius, center.y - radius, radius * 2, radius * 2};
        if (SDL_PointInRect(&pt, &rect)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

float FrameChildrenEditor::canvas_pixels_per_unit() const {
    if (canvas_) {
        float direct = canvas_->screen_pixels_per_unit();
        if (std::isfinite(direct) && direct > 0.0f) {
            return direct;
        }
        SDL_FPoint origin = canvas_->world_to_screen(SDL_FPoint{0.0f, 0.0f});
        SDL_FPoint offset_x = canvas_->world_to_screen(SDL_FPoint{1.0f, 0.0f});
        float dx = std::fabs(offset_x.x - origin.x);
        if (std::isfinite(dx) && dx > 0.001f) {
            return dx;
        }
        SDL_FPoint offset_y = canvas_->world_to_screen(SDL_FPoint{0.0f, 1.0f});
        float dy = std::fabs(offset_y.y - origin.y);
        if (std::isfinite(dy) && dy > 0.001f) {
            return dy;
        }
    }
    return 1.0f;
}

float FrameChildrenEditor::document_scale_factor() const {
    if (canvas_) {
        float scale = canvas_->document_scale_factor();
        if (std::isfinite(scale) && scale > 0.0f) {
            return scale;
        }
    }
    if (document_) {
        float pct = static_cast<float>(document_->scale_percentage());
        if (std::isfinite(pct) && pct > 0.0f) {
            return pct / 100.0f;
        }
    }
    return 1.0f;
}

float FrameChildrenEditor::child_scale_percentage(const std::string& child_id) const {
    float fallback = 100.0f;
    if (child_id.empty()) {
        return fallback;
    }
    auto cached = child_scale_cache_.find(child_id);
    if (cached != child_scale_cache_.end()) {
        return cached->second;
    }

    auto try_cache = [&](const std::string& candidate) -> bool {
        if (candidate.empty()) {
            return false;
        }
        float scale = lookup_scale_from_manifest(candidate);
        if (std::isfinite(scale) && scale > 0.0f) {
            child_scale_cache_[child_id] = scale;
            return true;
        }
        return false;
};

    if (try_cache(child_id)) {
        return child_scale_cache_[child_id];
    }

    fs::path child_dir = resolve_child_asset_directory(child_id);
    if (!child_dir.empty()) {
        std::string leaf = child_dir.filename().string();
        if (!leaf.empty() && !iequals(leaf, child_id)) {
            if (try_cache(leaf)) {
                return child_scale_cache_[child_id];
            }
        }
    }

    child_scale_cache_[child_id] = fallback;
    return fallback;
}

std::filesystem::path FrameChildrenEditor::resolve_assets_root() const {
    if (cached_assets_root_valid_) {
        return cached_assets_root_;
    }
    cached_assets_root_valid_ = true;
    cached_assets_root_.clear();
    if (!document_) {
        return cached_assets_root_;
    }
    fs::path root = document_->asset_root();
    if (root.empty()) {
        root = document_->info_path().parent_path();
    }
    if (root.empty()) {
        return cached_assets_root_;
    }
    fs::path search = root;
    while (!search.empty()) {
        if (iequals(search.filename().string(), "assets")) {
            cached_assets_root_ = search;
            break;
        }
        search = search.parent_path();
    }
    if (cached_assets_root_.empty()) {
        fs::path parent = root.parent_path();
        if (parent.empty()) {
            cached_assets_root_ = root;
        } else {
            cached_assets_root_ = parent;
        }
    }
    return cached_assets_root_;
}

float FrameChildrenEditor::lookup_scale_from_manifest(const std::string& key) const {
    if (key.empty()) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    ensure_manifest_scale_cache();
    if (!manifest_scale_cache_valid_) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    std::string lookup_key = lowercase_copy(key);
    auto it = manifest_scale_cache_.find(lookup_key);
    if (it != manifest_scale_cache_.end()) {
        return it->second;
    }
    return std::numeric_limits<float>::quiet_NaN();
}

void FrameChildrenEditor::ensure_manifest_scale_cache() const {
    if (manifest_scale_cache_valid_) {
        return;
    }
    manifest_scale_cache_valid_ = true;
    manifest_scale_cache_.clear();

    fs::path manifest_path = resolve_manifest_path();
    if (manifest_path.empty()) {
        return;
    }
    std::ifstream in(manifest_path);
    if (!in.good()) {
        return;
    }
    nlohmann::json manifest = nlohmann::json::object();
    try {
        in >> manifest;
    } catch (...) {
        return;
    }
    auto assets_it = manifest.find("assets");
    if (assets_it == manifest.end() || !assets_it->is_object()) {
        return;
    }
    for (const auto& item : assets_it->items()) {
        if (!item.value().is_object()) {
            continue;
        }
        const auto size_it = item.value().find("size_settings");
        if (size_it == item.value().end() || !size_it->is_object()) {
            continue;
        }
        const auto scale_it = size_it->find("scale_percentage");
        if (scale_it == size_it->end() || !scale_it->is_number()) {
            continue;
        }
        double pct = scale_it->get<double>();
        if (!std::isfinite(pct) || pct <= 0.0) {
            continue;
        }
        manifest_scale_cache_.emplace(lowercase_copy(item.key()), static_cast<float>(pct));
    }
}

std::filesystem::path FrameChildrenEditor::resolve_manifest_path() const {
    if (cached_manifest_path_valid_) {
        return cached_manifest_path_;
    }
    cached_manifest_path_valid_ = true;
    cached_manifest_path_.clear();

    auto attempt = [&](const fs::path& candidate) -> bool {
        if (candidate.empty()) {
            return false;
        }
        std::error_code ec;
        if (fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec)) {
            cached_manifest_path_ = candidate;
            return true;
        }
        return false;
};

    fs::path start;
    if (document_) {
        start = document_->asset_root();
        if (start.empty()) {
            start = document_->info_path().parent_path();
        }
    }
    if (start.empty()) {
        start = resolve_assets_root();
    }
    if (start.empty()) {
        std::error_code ec;
        start = fs::current_path(ec);
    }

    fs::path search = start;
    while (!search.empty()) {
        if (attempt(search / "manifest.json")) {
            break;
        }
        fs::path parent = search.parent_path();
        if (parent == search) {
            break;
        }
        search = parent;
    }
    if (cached_manifest_path_.empty()) {
        attempt(fs::path("manifest.json"));
    }
    return cached_manifest_path_;
}

std::filesystem::path FrameChildrenEditor::resolve_child_asset_directory(const std::string& child_id) const {
    if (child_id.empty() || child_id.front() == '#') {
        return {};
    }
    auto it = child_asset_dir_cache_.find(child_id);
    if (it != child_asset_dir_cache_.end()) {
        return it->second;
    }
    fs::path child_path(child_id);
    std::error_code ec;
    if (child_path.is_absolute()) {
        if (fs::exists(child_path, ec)) {
            return child_asset_dir_cache_.emplace(child_id, child_path).first->second;
        }
        return child_asset_dir_cache_.emplace(child_id, fs::path{}).first->second;
    }
    auto try_match = [&](const fs::path& base) -> fs::path {
        if (base.empty()) {
            return {};
        }
        fs::path candidate = base / child_path;
        if (fs::exists(candidate, ec)) {
            return candidate;
        }
        if (!fs::is_directory(base, ec)) {
            return {};
        }
        for (const auto& entry : fs::directory_iterator(base, ec)) {
            if (ec) break;
            if (!entry.is_directory(ec)) continue;
            if (iequals(entry.path().filename().string(), child_id)) {
                return entry.path();
            }
        }
        return {};
};
    fs::path resolved = try_match(resolve_assets_root());
    if (resolved.empty() && document_) {
        resolved = try_match(document_->asset_root().parent_path());
        if (resolved.empty()) {
            resolved = try_match(document_->asset_root());
        }
    }
    return child_asset_dir_cache_.emplace(child_id, resolved).first->second;
}

std::filesystem::path FrameChildrenEditor::find_first_frame_in_folder(const std::filesystem::path& folder) const {
    std::error_code ec;
    if (folder.empty() || !fs::exists(folder, ec) || !fs::is_directory(folder, ec)) {
        return {};
    }
    for (int i = 0; i < 32; ++i) {
        fs::path candidate = folder / (std::to_string(i) + ".png");
        if (fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec)) {
            return candidate;
        }
    }
    std::vector<fs::path> numbered;
    fs::path fallback;
    for (const auto& entry : fs::directory_iterator(folder, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        fs::path path = entry.path();
        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        if (ext != ".png") {
            continue;
        }
        if (fallback.empty()) {
            fallback = path;
        }
        if (has_numeric_stem(path)) {
            numbered.push_back(path);
        }
    }
    if (!numbered.empty()) {
        std::sort(numbered.begin(), numbered.end(), [](const fs::path& a, const fs::path& b) {
            int lhs = 0;
            int rhs = 0;
            try {
                lhs = std::stoi(a.stem().string());
            } catch (...) {
                lhs = 0;
            }
            try {
                rhs = std::stoi(b.stem().string());
            } catch (...) {
                rhs = 0;
            }
            if (lhs == rhs) {
                return a.filename().string() < b.filename().string();
            }
            return lhs < rhs;
        });
        return numbered.front();
    }
    return fallback;
}

std::filesystem::path FrameChildrenEditor::resolve_child_frame_path(const std::string& child_id) const {
    if (child_id.empty() || child_id.front() == '#') {
        return {};
    }
    fs::path asset_dir = resolve_child_asset_directory(child_id);
    if (asset_dir.empty()) {
        return {};
    }
    std::error_code ec;
    if (!fs::exists(asset_dir, ec) || !fs::is_directory(asset_dir, ec)) {
        return {};
    }
    fs::path default_dir = asset_dir / "default";
    if (fs::exists(default_dir, ec) && fs::is_directory(default_dir, ec)) {
        fs::path frame = find_first_frame_in_folder(default_dir);
        if (!frame.empty()) {
            return frame;
        }
    }
    for (const auto& entry : fs::directory_iterator(asset_dir, ec)) {
        if (ec) break;
        if (!entry.is_directory(ec)) continue;
        fs::path frame = find_first_frame_in_folder(entry.path());
        if (!frame.empty()) {
            return frame;
        }
    }
    return find_first_frame_in_folder(asset_dir);
}

SDL_Texture* FrameChildrenEditor::acquire_child_texture(SDL_Renderer* renderer,
                                                        const std::string& child_id,
                                                        int* tex_w,
                                                        int* tex_h) const {
    if (tex_w) *tex_w = 0;
    if (tex_h) *tex_h = 0;
    if (!renderer || child_id.empty() || child_id.front() == '#') {
        return nullptr;
    }
    fs::path frame_path = resolve_child_frame_path(child_id);
    if (frame_path.empty()) {
        child_previews_.erase(child_id);
        return nullptr;
    }
    std::error_code ec;
    bool has_timestamp = false;
    fs::file_time_type timestamp{};
    if (fs::exists(frame_path, ec) && fs::is_regular_file(frame_path, ec)) {
        timestamp = fs::last_write_time(frame_path, ec);
        has_timestamp = !ec;
    }
    auto it = child_previews_.find(child_id);
    if (it != child_previews_.end()) {
        const ChildPreviewTexture& cached = it->second;
        bool renderer_matches = cached.renderer == renderer;
        bool source_matches = cached.source_path == frame_path;
        bool timestamp_matches = true;
        if (has_timestamp && cached.has_timestamp && cached.last_write_time != timestamp) {
            timestamp_matches = false;
        } else if (has_timestamp != cached.has_timestamp) {
            timestamp_matches = false;
        }
        if (renderer_matches && source_matches && timestamp_matches && cached.texture) {
            if (tex_w) *tex_w = cached.width;
            if (tex_h) *tex_h = cached.height;
            return cached.texture.get();
        }
    }
    SDL_Surface* surface = IMG_Load(frame_path.string().c_str());
    if (!surface) {
        child_previews_.erase(child_id);
        return nullptr;
    }
    SDL_Surface* converted = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(surface);
    if (!converted) {
        child_previews_.erase(child_id);
        return nullptr;
    }
    SDL_Surface* scaled_surface = nullptr;
    float child_scale = child_scale_percentage(child_id) / 100.0f;
    if (!std::isfinite(child_scale) || child_scale <= 0.0f) {
        child_scale = 1.0f;
    }
    if (std::fabs(child_scale - 1.0f) > 1e-4f) {
        scaled_surface = render_pipeline::CreateScaledSurface(converted, child_scale);
        if (scaled_surface) {
            SDL_FreeSurface(converted);
        } else {
            scaled_surface = nullptr;
        }
    }
    SDL_Surface* working = scaled_surface ? scaled_surface : converted;
    SDL_Texture* raw = SDL_CreateTextureFromSurface(renderer, working);
    int width = working ? working->w : 0;
    int height = working ? working->h : 0;
    SDL_FreeSurface(working);
    if (!raw) {
        child_previews_.erase(child_id);
        return nullptr;
    }
    SDL_SetTextureBlendMode(raw, SDL_BLENDMODE_BLEND);
    ChildPreviewTexture entry;
    entry.renderer = renderer;
    entry.texture.reset(raw, SDL_DestroyTexture);
    entry.source_path = frame_path;
    entry.last_write_time = timestamp;
    entry.has_timestamp = has_timestamp;
    entry.width = width;
    entry.height = height;
    child_previews_[child_id] = entry;
    if (tex_w) *tex_w = width;
    if (tex_h) *tex_h = height;
    return child_previews_[child_id].texture.get();
}

}
