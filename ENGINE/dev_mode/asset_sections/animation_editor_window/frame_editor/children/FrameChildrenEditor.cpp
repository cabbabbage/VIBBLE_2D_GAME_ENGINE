#include "FrameChildrenEditor.hpp"

#include <SDL_ttf.h>

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

#include "../../AnimationDocument.hpp"
#include "../FrameToolsPanel.hpp"
#include "../movement/MovementCanvas.hpp"
#include "../../../../dm_styles.hpp"
#include "../../../../draw_utils.hpp"

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

}  // namespace

FrameChildrenEditor::FrameChildrenEditor() = default;

void FrameChildrenEditor::set_document(std::shared_ptr<AnimationDocument> document) {
    if (document_ == document) {
        return;
    }
    document_ = std::move(document);
    payload_signature_.clear();
    reload_from_document();
}

void FrameChildrenEditor::set_animation_id(const std::string& animation_id) {
    if (animation_id_ == animation_id) {
        return;
    }
    animation_id_ = animation_id;
    payload_signature_.clear();
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
            [this](bool visible) { this->set_child_visible(visible); });
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

void FrameChildrenEditor::update() {
    if (!document_ || animation_id_.empty()) {
        return;
    }
    auto payload_dump = document_->animation_payload(animation_id_);
    std::string signature = payload_dump.has_value() ? *payload_dump : std::string{};
    if (payload_signature_ != signature) {
        payload_signature_ = signature;
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
    for (std::size_t i = 0; i < child_ids_.size() && i < frame->children.size(); ++i) {
        const auto& child = frame->children[i];
        SDL_FPoint screen = world_to_screen(SDL_FPoint{child.dx, child.dy});
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
                drag_start_world_ = screen_to_world(drag_start_screen_);
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
            SDL_FPoint world = screen_to_world(screen);
            if (auto* child = current_child()) {
                child->dx = static_cast<float>(std::round(world.x));
                child->dy = static_cast<float>(std::round(world.y));
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
    frames_.clear();
    child_ids_.clear();
    selected_child_index_ = 0;

    if (payload_signature_.empty()) {
        frames_.push_back(MovementFrame{});
        refresh_tools_panel();
        return;
    }

    nlohmann::json payload = nlohmann::json::parse(payload_signature_, nullptr, false);
    if (!payload.is_object()) {
        payload = nlohmann::json::object();
    }

    if (payload.contains("children") && payload["children"].is_array()) {
        for (const auto& entry : payload["children"]) {
            if (!entry.is_string()) continue;
            std::string name = entry.get<std::string>();
            if (name.empty()) continue;
            child_ids_.push_back(std::move(name));
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
                if (entry.size() > 4 && entry[4].is_array()) {
                    for (const auto& child_entry : entry[4]) {
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
                            child.rotation = static_cast<float>(child_entry.value("rotation", 0.0));
                            child.visible = child_entry.value("visible", true);
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
    selected_frame_index_ = std::clamp(selected_frame_index_, 0, static_cast<int>(frames_.size()) - 1);

    refresh_tools_panel();
}

void FrameChildrenEditor::ensure_child_vectors() {
    if (child_ids_.empty()) {
        for (auto& frame : frames_) {
            frame.children.clear();
        }
        selected_child_index_ = 0;
        return;
    }
    for (auto& frame : frames_) {
        std::vector<ChildFrame> normalized(child_ids_.size());
        for (std::size_t i = 0; i < normalized.size(); ++i) {
            normalized[i].child_index = static_cast<int>(i);
            normalized[i].visible = true;
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

void FrameChildrenEditor::refresh_tools_panel() const {
    if (!tools_panel_) {
        return;
    }
    bool has_children = !child_ids_.empty();
    bool visible = true;
    if (const ChildFrame* child = current_child()) {
        visible = child->visible;
    }
    tools_panel_->set_children_state(child_ids_, selected_child_index_, visible, has_children);
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

void FrameChildrenEditor::persist_changes() {
    if (!document_ || animation_id_.empty()) {
        return;
    }
    nlohmann::json payload = nlohmann::json::object();
    if (!payload_signature_.empty()) {
        nlohmann::json parsed = nlohmann::json::parse(payload_signature_, nullptr, false);
        if (parsed.is_object()) {
            payload = parsed;
        }
    }

    nlohmann::json children_json = nlohmann::json::array();
    for (const auto& child_name : child_ids_) {
        children_json.push_back(child_name);
    }
    if (!children_json.empty()) {
        payload["children"] = std::move(children_json);
    }

    nlohmann::json movement_json = nlohmann::json::array();
    for (std::size_t i = 0; i < frames_.size(); ++i) {
        const auto& frame = frames_[i];
        int dx = static_cast<int>(std::lround(i == 0 ? 0.0f : frame.dx));
        int dy = static_cast<int>(std::lround(i == 0 ? 0.0f : frame.dy));
        nlohmann::json entry = nlohmann::json::array({dx, dy});
        if (frame.resort_z) {
            entry.push_back(frame.resort_z);
        }
        if (!child_ids_.empty()) {
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
    movement_json[0][0] = 0;
    movement_json[0][1] = 0;
    payload["movement"] = std::move(movement_json);

    int total_dx = 0;
    int total_dy = 0;
    for (std::size_t i = 1; i < frames_.size(); ++i) {
        total_dx += static_cast<int>(std::lround(frames_[i].dx));
        total_dy += static_cast<int>(std::lround(frames_[i].dy));
    }
    payload["movement_total"] = nlohmann::json{{"dx", total_dx}, {"dy", total_dy}};

    document_->replace_animation_payload(animation_id_, payload.dump());
    document_->save_to_file();
    auto refreshed = document_->animation_payload(animation_id_);
    payload_signature_ = refreshed.has_value() ? *refreshed : std::string{};
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

int FrameChildrenEditor::hit_test_child(int x, int y) const {
    const MovementFrame* frame = current_frame();
    if (!frame) {
        return -1;
    }
    SDL_Point pt{x, y};
    for (std::size_t i = 0; i < child_ids_.size() && i < frame->children.size(); ++i) {
        SDL_FPoint screen = world_to_screen(SDL_FPoint{frame->children[i].dx, frame->children[i].dy});
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

}  // namespace animation_editor
