#include "FrameChildrenEditor.hpp"

#include <SDL_image.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <nlohmann/json.hpp>

#include "../../AnimationDocument.hpp"
#include "../FrameToolsPanel.hpp"
#include "../movement/MovementCanvas.hpp"
#include "../../../../dm_styles.hpp"
#include "../../../../draw_utils.hpp"

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

}  // namespace

FrameChildrenEditor::FrameChildrenEditor() = default;

void FrameChildrenEditor::set_document(std::shared_ptr<AnimationDocument> document) {
    if (document_ == document) {
        return;
    }
    document_ = std::move(document);
    payload_signature_.clear();
    invalidate_child_caches();
    reload_from_document();
}

void FrameChildrenEditor::set_animation_id(const std::string& animation_id) {
    if (animation_id_ == animation_id) {
        return;
    }
    animation_id_ = animation_id;
    payload_signature_.clear();
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
    float base_scale_pct = 100.0f;
    if (document_) {
        base_scale_pct = static_cast<float>(document_->scale_percentage());
    }
    if (!std::isfinite(base_scale_pct) || base_scale_pct <= 0.0f) {
        base_scale_pct = 100.0f;
    }
    float pixels_per_unit = canvas_pixels_per_unit();
    if (!std::isfinite(pixels_per_unit) || pixels_per_unit <= 0.0f) {
        pixels_per_unit = 1.0f;
    }
    const float sprite_scale = (base_scale_pct / 100.0f) * pixels_per_unit;

    if (sprite_scale > 0.0f) {
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
            SDL_FPoint screen = world_to_screen(SDL_FPoint{child.dx, child.dy});
            const float dst_w = sprite_scale * static_cast<float>(tex_w);
            const float dst_h = sprite_scale * static_cast<float>(tex_h);
            if (!(std::isfinite(dst_w) && std::isfinite(dst_h)) || dst_w <= 0.0f || dst_h <= 0.0f) {
                continue;
            }
            SDL_Rect dst{static_cast<int>(std::round(screen.x - dst_w * 0.5f)),
                         static_cast<int>(std::round(screen.y - dst_h)),
                         static_cast<int>(std::round(dst_w)),
                         static_cast<int>(std::round(dst_h))};
            if (dst.w <= 0 || dst.h <= 0) {
                continue;
            }
            SDL_Point pivot{dst.w / 2, dst.h};
            SDL_RenderCopyEx(renderer, texture, nullptr, &dst, child.rotation, &pivot, SDL_FLIP_NONE);
        }
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
    std::vector<std::string> previous_children = child_ids_;
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
                            child.rotation = static_cast<float>(child_entry.value("rotation", 0.0));
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
        return;
    }
    for (auto& frame : frames_) {
        std::vector<ChildFrame> normalized(child_ids_.size());
        for (std::size_t i = 0; i < normalized.size(); ++i) {
            normalized[i].child_index = static_cast<int>(i);
            normalized[i].visible = true;
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

void FrameChildrenEditor::invalidate_child_caches() {
    child_previews_.clear();
    child_asset_dir_cache_.clear();
    cached_assets_root_.clear();
    cached_assets_root_valid_ = false;
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

float FrameChildrenEditor::canvas_pixels_per_unit() const {
    if (!canvas_) {
        return 1.0f;
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
    return 1.0f;
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
    SDL_Texture* raw = SDL_CreateTextureFromSurface(renderer, converted);
    int width = converted->w;
    int height = converted->h;
    SDL_FreeSurface(converted);
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

}  // namespace animation_editor
