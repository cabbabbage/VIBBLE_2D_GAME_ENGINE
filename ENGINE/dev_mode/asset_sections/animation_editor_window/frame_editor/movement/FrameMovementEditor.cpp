#include "FrameMovementEditor.hpp"

#include <SDL.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
#include <utility>

#include "../../AnimationDocument.hpp"
#include "../../PanelLayoutConstants.hpp"
#include "../../../../dm_styles.hpp"
#include "../../../../draw_utils.hpp"
#include "../../../../widgets.hpp"
#include "../../PreviewProvider.hpp"
#include "FramePropertiesPanel.hpp"
#include "MovementCanvas.hpp"
#include "TotalsPanel.hpp"

namespace animation_editor {

namespace {

constexpr int kTotalsHeight = 0;
constexpr int kVariantHeaderPadding = kPanelPadding;
constexpr int kVariantTabHeight = 28;
constexpr int kVariantTabSpacing = 6;
constexpr int kVariantTabWidth = 140;
constexpr int kVariantCloseSize = 18;

constexpr int kFrameListBaseSize = 64;
constexpr int kFrameListMaxSize  = 144;
constexpr int kFrameListMinSize = 36;
constexpr int kFrameThumbnailPadding = 6;
constexpr int kFrameListTitleHeight = 22;

constexpr int kFrameListScrollbarHeight = 18;
constexpr int kScrollbarMinKnobWidth   = 32;

int clamp_index(int index, int max_value) {
    if (max_value <= 0) return 0;
    return std::clamp(index, 0, max_value - 1);
}

void sanitize_frames(std::vector<MovementFrame>& frames) {
    if (frames.empty()) {
        frames.push_back(MovementFrame{});
    }
    if (frames.empty()) return;
    for (auto& frame : frames) {
        if (!std::isfinite(frame.dx)) frame.dx = 0.0f;
        if (!std::isfinite(frame.dy)) frame.dy = 0.0f;
    }
}

std::vector<MovementFrame> parse_movement_frames(const nlohmann::json& payload) {
    std::vector<MovementFrame> frames;
    if (!payload.is_array()) {
        frames.push_back(MovementFrame{});
        return frames;
    }
    for (const auto& entry : payload) {
        MovementFrame frame;
        if (entry.is_array()) {
            if (!entry.empty() && entry[0].is_number()) {
                frame.dx = entry[0].get<float>();
            }
            if (entry.size() > 1 && entry[1].is_number()) {
                frame.dy = entry[1].get<float>();
            }
            if (entry.size() > 2 && entry[2].is_boolean()) {
                frame.resort_z = entry[2].get<bool>();
            }
        } else if (entry.is_object()) {
            frame.dx = entry.value("dx", 0.0f);
            frame.dy = entry.value("dy", 0.0f);
            frame.resort_z = entry.value("resort_z", false);
        }
        frames.push_back(frame);
    }
    if (frames.empty()) {
        frames.push_back(MovementFrame{});
    }
    sanitize_frames(frames);
    return frames;
}

nlohmann::json serialize_frames_to_json(const std::vector<MovementFrame>& frames) {
    nlohmann::json movement = nlohmann::json::array();
    for (size_t i = 0; i < frames.size(); ++i) {
        const MovementFrame& frame = frames[i];
        int dx = static_cast<int>(std::lround(frame.dx));
        int dy = static_cast<int>(std::lround(frame.dy));
        nlohmann::json entry = nlohmann::json::array({dx, dy});
        if (frame.resort_z) {
            entry.push_back(frame.resort_z);
        }
        movement.push_back(entry);
    }
    if (movement.empty()) {
        movement.push_back(nlohmann::json::array({0, 0}));
    }
    return movement;
}

bool frames_equal(const std::vector<MovementFrame>& a, const std::vector<MovementFrame>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const MovementFrame& lhs = a[i];
        const MovementFrame& rhs = b[i];
        if (lhs.resort_z != rhs.resort_z) return false;
        if (std::fabs(lhs.dx - rhs.dx) > 0.001f) return false;
        if (std::fabs(lhs.dy - rhs.dy) > 0.001f) return false;
    }
    return true;
}

void render_tab_text(SDL_Renderer* renderer, const std::string& text, const SDL_Rect& rect, SDL_Color color) {
    if (!renderer || text.empty()) {
        return;
    }

    const DMLabelStyle& style = DMStyles::Label();
    TTF_Font* font = style.open_font();
    if (!font) {
        return;
    }

    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (!surface) {
        TTF_CloseFont(font);
        return;
    }

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture) {
        SDL_Rect dst{rect.x + (rect.w - surface->w) / 2, rect.y + (rect.h - surface->h) / 2, surface->w, surface->h};
        SDL_RenderCopy(renderer, texture, nullptr, &dst);
        SDL_DestroyTexture(texture);
    }

    SDL_FreeSurface(surface);
    TTF_CloseFont(font);
}

void render_badge_text_small(SDL_Renderer* renderer, const std::string& text, const SDL_Rect& rect, SDL_Color color) {
    if (!renderer || text.empty()) {
        return;
    }

    DMLabelStyle style = DMStyles::Label();
    style.font_size = std::max(10, style.font_size - 2);
    TTF_Font* font = style.open_font();
    if (!font) {
        return;
    }

    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (!surface) {
        TTF_CloseFont(font);
        return;
    }

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture) {
        SDL_Rect dst{rect.x + (rect.w - surface->w) / 2, rect.y + (rect.h - surface->h) / 2, surface->w, surface->h};
        SDL_RenderCopy(renderer, texture, nullptr, &dst);
        SDL_DestroyTexture(texture);
    }

    SDL_FreeSurface(surface);
    TTF_CloseFont(font);
}

std::vector<MovementFrame> default_variant_frames() {
    return parse_movement_frames(nlohmann::json::array());
}

}

FrameMovementEditor::FrameMovementEditor() { ensure_children(); }

int FrameMovementEditor::view_frame_count() const {
    if (frames_.empty()) {
        return 0;
    }
    if (frame_list_override_count_ > 0) {
        return frame_list_override_count_;
    }
    return static_cast<int>(frames_.size());
}

int FrameMovementEditor::map_view_to_actual(int view_index) const {
    if (frames_.empty()) {
        return 0;
    }
    int view_count = view_frame_count();
    if (view_count <= 0) {
        return 0;
    }
    view_index = std::clamp(view_index, 0, view_count - 1);
    const int base_count = static_cast<int>(frames_.size());
    if (frame_list_override_count_ <= 0 || frame_list_override_count_ <= base_count) {
        return std::min(view_index, base_count - 1);
    }
    if (base_count == 0) {
        return 0;
    }
    return view_index % base_count;
}

int FrameMovementEditor::view_index_for_actual(int actual_index) const {
    if (frames_.empty()) {
        return 0;
    }
    int view_count = view_frame_count();
    if (view_count <= 0) {
        return 0;
    }
    const int base_count = static_cast<int>(frames_.size());
    actual_index = std::clamp(actual_index, 0, base_count - 1);
    if (frame_list_override_count_ <= 0 || frame_list_override_count_ <= base_count) {
        return std::min(actual_index, view_count - 1);
    }
    if (display_selected_index_ >= 0 && display_selected_index_ < view_count &&
        map_view_to_actual(display_selected_index_) == actual_index) {
        return display_selected_index_;
    }
    return actual_index;
}

int FrameMovementEditor::clamp_view_index(int index) const {
    int view_count = view_frame_count();
    if (view_count <= 0) {
        return 0;
    }
    return std::clamp(index, 0, view_count - 1);
}

void FrameMovementEditor::sync_view_selection_from_actual() {
    selected_index_ = clamp_index(selected_index_, static_cast<int>(frames_.size()));
    display_selected_index_ = clamp_view_index(view_index_for_actual(selected_index_));
}

void FrameMovementEditor::set_document(std::shared_ptr<AnimationDocument> document) {
    if (document_ == document && !frames_.empty()) {
        return;
    }
    document_ = std::move(document);
    load_frames_from_document();
}

void FrameMovementEditor::set_animation_id(const std::string& animation_id) {
    if (animation_id_ == animation_id && !frames_.empty()) {
        return;
    }
    animation_id_ = animation_id;
    load_frames_from_document();
}

void FrameMovementEditor::set_layout_sections(const SDL_Rect& mode_controls_bounds,
                                              const SDL_Rect& frame_display_bounds,
                                              const SDL_Rect& frame_list_bounds) {
    mode_controls_rect_ = mode_controls_bounds;
    frame_display_rect_ = frame_display_bounds;
    frame_list_rect_ = frame_list_bounds;
    update_layout();
}

void FrameMovementEditor::set_close_callback(CloseCallback callback) { close_callback_ = std::move(callback); }

void FrameMovementEditor::set_preview_provider(std::shared_ptr<PreviewProvider> provider) {
    preview_provider_ = std::move(provider);
}

void FrameMovementEditor::set_frame_list_override(int count, const std::string& animation_id, bool preserve_selection) {
    int normalized_count = (count > 0 && !frames_.empty()) ? count : -1;
    std::string normalized_id = normalized_count > 0 ? animation_id : std::string{};
    if (frame_list_override_count_ == normalized_count && frame_list_override_animation_id_ == normalized_id) {
        return;
    }
    frame_list_override_count_ = normalized_count;
    frame_list_override_animation_id_ = std::move(normalized_id);
    if (!preserve_selection) {
        display_selected_index_ = clamp_view_index(display_selected_index_);
    }
    sync_view_selection_from_actual();
    selected_index_ = map_view_to_actual(display_selected_index_);
    synchronize_selection();
    layout_frame_list();
    ensure_selection_visible();
}

void FrameMovementEditor::update() {
    ensure_children();
    if (canvas_) {
        canvas_->update();
        if (selected_index_ != canvas_->selected_index()) {
            selected_index_ = canvas_->selected_index();
            sync_view_selection_from_actual();
            synchronize_selection();
        }

        int hover = canvas_->hovered_index();
        if (hover >= 0 && hover < static_cast<int>(frames_.size())) {
            hovered_frame_index_ = view_index_for_actual(hover);
        } else {
            hovered_frame_index_ = -1;
        }

        if (preview_provider_) {
            float pct = 100.0f;
            if (document_) {
                pct = static_cast<float>(document_->scale_percentage());
            }
            canvas_->set_animation_context(preview_provider_, animation_id_, pct);
            canvas_->set_show_animation_overlay(show_animation_);
        }
    }
    if (totals_panel_) totals_panel_->update();
    if (properties_panel_) {
        properties_panel_->update();
        if (properties_panel_->take_dirty_flag()) {
            mark_dirty();
        }
    }

    if (dirty_) {
        apply_changes();
        dirty_ = false;
    }
}

void FrameMovementEditor::render(SDL_Renderer* renderer) const {
    render_variant_header(renderer);

    if (canvas_) canvas_->render(renderer);
    if (totals_panel_) totals_panel_->render(renderer);
    if (properties_panel_) properties_panel_->render(renderer);
    render_frame_list(renderer);
}

void FrameMovementEditor::render_canvas_only(SDL_Renderer* renderer) const {
    if (canvas_) canvas_->render_background(renderer);
}

bool FrameMovementEditor::handle_event(const SDL_Event& e) {

    if (handle_variant_header_event(e)) {
        return true;
    }

    if (handle_frame_list_event(e)) {
        return true;
    }

    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
        if (close_callback_) close_callback_();
        return true;
    }

    bool consumed = false;
    if (canvas_ && canvas_->handle_event(e)) {
        auto updated_frames = canvas_->frames();
        sanitize_frames(updated_frames);
        bool changed = !frames_equal(frames_, updated_frames);
        frames_ = std::move(updated_frames);
        selected_index_ = canvas_->selected_index();
        if (totals_panel_) totals_panel_->set_frames(frames_);
        if (properties_panel_) {
            properties_panel_->set_frames(&frames_);
            properties_panel_->refresh_from_selection();
        }
        layout_frame_list();
        if (changed) {
            mark_dirty();
        } else {
            synchronize_selection();
        }
        consumed = true;
    }

    if (totals_panel_ && totals_panel_->handle_event(e)) {
        synchronize_selection();
        consumed = true;
    }

    if (properties_panel_ && properties_panel_->handle_event(e)) {
        mark_dirty();
        consumed = true;
    }

    return consumed;
}

bool FrameMovementEditor::can_select_previous_frame() const {
    if (view_frame_count() <= 0) return false;
    return display_selected_index_ > 0;
}

bool FrameMovementEditor::can_select_next_frame() const {
    int view_count = view_frame_count();
    if (view_count <= 0) return false;
    return display_selected_index_ < view_count - 1;
}

void FrameMovementEditor::select_previous_frame() {
    display_selected_index_ = clamp_view_index(display_selected_index_);
    if (display_selected_index_ <= 0) return;
    --display_selected_index_;
    selected_index_ = map_view_to_actual(display_selected_index_);
    ensure_selection_visible();
    synchronize_selection();
}

void FrameMovementEditor::select_next_frame() {
    int view_count = view_frame_count();
    display_selected_index_ = clamp_view_index(display_selected_index_);
    if (view_count <= 0) return;
    if (display_selected_index_ >= view_count - 1) return;
    ++display_selected_index_;
    selected_index_ = map_view_to_actual(display_selected_index_);
    ensure_selection_visible();
    synchronize_selection();
}

void FrameMovementEditor::load_frames_from_document() {
    ensure_children();
    frames_.clear();
    selected_index_ = 0;
    variants_.clear();
    variant_tabs_.clear();
    active_variant_index_ = 0;

    if (!document_ || animation_id_.empty()) {
        MovementVariant variant;
        variant.name = "Primary";
        variant.primary = true;
        variant.frames = default_variant_frames();
        variants_.push_back(std::move(variant));
    } else {
        auto payload_dump = document_->animation_payload(animation_id_);
        nlohmann::json payload = nlohmann::json::object();
        if (payload_dump.has_value()) {
            payload = nlohmann::json::parse(*payload_dump, nullptr, false);
            if (!payload.is_object()) {
                payload = nlohmann::json::object();
            }
        }

        nlohmann::json movement = nlohmann::json::array();
        if (payload.contains("movement")) {
            movement = payload["movement"];
        }

        MovementVariant primary;
        primary.name = "Primary";
        primary.primary = true;
        primary.frames = parse_movement_frames(movement);

        const auto extract_declared_frames = [](const nlohmann::json& object) -> int {
            if (!object.is_object()) {
                return 0;
            }
            auto it = object.find("number_of_frames");
            if (it == object.end()) {
                return 0;
            }
            const nlohmann::json& value = *it;
            try {
                if (value.is_number_integer()) {
                    return std::max(value.get<int>(), 0);
                }
                if (value.is_number()) {
                    return std::max(static_cast<int>(value.get<double>()), 0);
                }
                if (value.is_string()) {
                    return std::max(std::stoi(value.get<std::string>()), 0);
                }
            } catch (...) {
            }
            return 0;
};

        const int declared_frame_count = extract_declared_frames(payload);

        bool derived = false;
        bool inherit_movement = true;
        std::string derived_source_id;
        if (payload.contains("source") && payload["source"].is_object()) {
            const auto& src = payload["source"];
            std::string kind = src.value("kind", std::string{"folder"});
            derived = (kind == std::string{"animation"});
            inherit_movement = payload.value("inherit_source_movement", true);
            try {
                derived_source_id = src.value("name", std::string{});
                if (derived_source_id.empty()) derived_source_id = src.value("path", std::string{});
            } catch (...) { derived_source_id.clear(); }
        }

        const bool match_source_exactly = derived && !inherit_movement;
        const int preview_frame_count = (preview_provider_ && !animation_id_.empty()) ? preview_provider_->get_frame_count(animation_id_) : 0;
        const auto source_payload_frame_count = [&](const std::string& source_id) -> int {
            if (source_id.empty() || !document_) {
                return 0;
            }
            auto src_payload_dump = document_->animation_payload(source_id);
            if (!src_payload_dump.has_value()) {
                return 0;
            }
            nlohmann::json src_payload = nlohmann::json::parse(*src_payload_dump, nullptr, false);
            if (!src_payload.is_object()) {
                return 0;
            }
            return extract_declared_frames(src_payload);
};

        int target_frame_slots = 0;
        if (match_source_exactly) {
            if (preview_frame_count > 0) {
                target_frame_slots = preview_frame_count;
            } else {
                target_frame_slots = source_payload_frame_count(derived_source_id);
            }
            if (target_frame_slots <= 0) {
                target_frame_slots = declared_frame_count;
            }
        } else {
            target_frame_slots = declared_frame_count;
            if (preview_frame_count > 0) {
                target_frame_slots = std::max(target_frame_slots, preview_frame_count);
            }
            if (target_frame_slots <= 0) {
                target_frame_slots = preview_frame_count;
            }
        }
        if (target_frame_slots <= 0) {
            target_frame_slots = static_cast<int>(primary.frames.size());
        }
        if (target_frame_slots <= 0) {
            target_frame_slots = 1;
        }

        const auto ensure_frame_slots = [&](std::vector<MovementFrame>& frames) {
            if (target_frame_slots <= 0) {
                sanitize_frames(frames);
                return;
            }
            if (static_cast<int>(frames.size()) < target_frame_slots) {
                frames.reserve(target_frame_slots);
                while (static_cast<int>(frames.size()) < target_frame_slots) {
                    frames.push_back(MovementFrame{});
                }
            } else if (match_source_exactly && static_cast<int>(frames.size()) > target_frame_slots) {
                frames.resize(target_frame_slots);
            }
            sanitize_frames(frames);
};

        ensure_frame_slots(primary.frames);
        variants_.push_back(std::move(primary));

        if (payload.contains("movement_variants")) {
            const nlohmann::json& variants_json = payload["movement_variants"];
            if (variants_json.is_array()) {
                int generated_index = 1;
                for (const auto& entry : variants_json) {
                    MovementVariant variant;
                    variant.primary = false;
                    nlohmann::json movement_payload = entry;
                    if (entry.is_object()) {
                        variant.name = entry.value("name", "");
                        if (entry.contains("movement")) {
                            movement_payload = entry["movement"];
                        }
                    }
                    if (variant.name.empty()) {
                        variant.name = "Alternative " + std::to_string(generated_index);
                    }
                    ++generated_index;
                    variant.frames = parse_movement_frames(movement_payload);
                    ensure_frame_slots(variant.frames);
                    variants_.push_back(std::move(variant));
                }
            }
        }

        if (variants_.empty()) {
            MovementVariant variant;
            variant.name = "Primary";
            variant.primary = true;
            variant.frames = default_variant_frames();
            variants_.push_back(std::move(variant));
        }
    }

    frames_ = variants_[active_variant_index_].frames;
    sanitize_frames(frames_);
    selected_index_ = clamp_index(selected_index_, static_cast<int>(frames_.size()));
    sync_view_selection_from_actual();
    variant_tabs_.resize(variants_.size());

    update_child_frames(false);
    layout_variant_header();
    dirty_ = false;
}

void FrameMovementEditor::apply_changes() {
    if (!document_ || animation_id_.empty()) return;

    sync_active_variant_frames();

    auto payload_dump = document_->animation_payload(animation_id_);
    nlohmann::json payload = nlohmann::json::object();
    if (payload_dump.has_value()) {
        payload = nlohmann::json::parse(*payload_dump, nullptr, false);
        if (!payload.is_object()) {
            payload = nlohmann::json::object();
        }
    }

    if (variants_.empty()) {
        MovementVariant variant;
        variant.name = "Primary";
        variant.primary = true;
        variant.frames = frames_;
        variants_.push_back(std::move(variant));
    }

    payload["movement"] = serialize_frames_to_json(variants_.front().frames);

    auto compute_totals = [](const std::vector<MovementFrame>& frames) {
        struct Totals {
            int dx = 0;
            int dy = 0;
        } totals;
        if (frames.empty()) {
            return totals;
        }
        for (size_t i = 1; i < frames.size(); ++i) {
            totals.dx += static_cast<int>(std::lround(frames[i].dx));
            totals.dy += static_cast<int>(std::lround(frames[i].dy));
        }
        return totals;
};

    const auto totals = compute_totals(variants_.front().frames);
    payload["movement_total"] = nlohmann::json{{"dx", totals.dx}, {"dy", totals.dy}};

    if (variants_.size() > 1) {
        nlohmann::json variants_json = nlohmann::json::array();
        for (size_t i = 1; i < variants_.size(); ++i) {
            nlohmann::json entry = nlohmann::json::object();
            entry["name"] = variants_[i].name;
            entry["movement"] = serialize_frames_to_json(variants_[i].frames);
            variants_json.push_back(std::move(entry));
        }
        payload["movement_variants"] = std::move(variants_json);
    } else {
        payload.erase("movement_variants");
    }

    document_->replace_animation_payload(animation_id_, payload.dump());

    document_->save_to_file();
    if (totals_panel_) totals_panel_->set_frames(frames_);
}

void FrameMovementEditor::ensure_selection_visible() {
    if (frame_list_rect_.w <= 0 || frame_list_rect_.h <= 0) {
        return;
    }
    const int padding = kPanelPadding;
    const int viewport_left = frame_list_rect_.x + padding;
    const int viewport_width = std::max(0, frame_list_rect_.w - padding * 2);
    if (viewport_width <= 0) {
        return;
    }
    if (frame_item_rects_.empty()) {
        layout_frame_list();
    }
    if (display_selected_index_ < 0 || display_selected_index_ >= static_cast<int>(frame_item_rects_.size())) {
        return;
    }
    SDL_Rect item = frame_item_rects_[display_selected_index_];

    if (item.x < viewport_left) {
        int delta = viewport_left - item.x;
        hscroll_offset_px_ = std::max(0, hscroll_offset_px_ - delta);
        layout_frame_list();
        return;
    }

    int viewport_right = viewport_left + viewport_width;
    int item_right = item.x + item.w;
    if (item_right > viewport_right) {
        int delta = item_right - viewport_right;
        const int max_offset = std::max(0, hscroll_content_px_ - viewport_width);
        hscroll_offset_px_ = std::min(max_offset, hscroll_offset_px_ + delta);
        layout_frame_list();
    }
}

void FrameMovementEditor::ensure_children() {
    if (!canvas_) {
        canvas_ = std::make_unique<MovementCanvas>();
    }
    if (!totals_panel_) {
        totals_panel_ = std::make_unique<TotalsPanel>();
    }
    if (totals_panel_) {
        totals_panel_->set_selected_index(&selected_index_);
    }

    if (properties_panel_) {
        properties_panel_.reset();
    }
    update_layout();
}

void FrameMovementEditor::update_layout() {
    if (canvas_) canvas_->set_bounds(frame_display_rect_);

    if (mode_controls_rect_.w <= 0 || mode_controls_rect_.h <= 0) {
        header_rect_ = SDL_Rect{0, 0, 0, 0};
        totals_rect_ = SDL_Rect{0, 0, 0, 0};
        properties_rect_ = SDL_Rect{0, 0, 0, 0};
    } else {
        int header_height = std::min(mode_controls_rect_.h, kVariantTabHeight + kVariantHeaderPadding * 2);
        if (header_height < 0) header_height = 0;
        header_rect_ = SDL_Rect{mode_controls_rect_.x, mode_controls_rect_.y, mode_controls_rect_.w, header_height};

        const int content_x = mode_controls_rect_.x + kPanelPadding;
        const int content_y = header_rect_.y + header_rect_.h + kPanelPadding;
        const int content_w = std::max(0, mode_controls_rect_.w - kPanelPadding * 2);
        const int content_h = std::max(0, mode_controls_rect_.y + mode_controls_rect_.h - content_y - kPanelPadding);

        const int totals_height = std::min(content_h, kTotalsHeight);
        totals_rect_ = SDL_Rect{content_x, content_y, content_w, totals_height};
        properties_rect_ = SDL_Rect{0, 0, 0, 0};
    }

    if (totals_panel_) totals_panel_->set_bounds(totals_rect_);

    layout_variant_header();

    smooth_button_rect_ = SDL_Rect{0,0,0,0};
    show_anim_button_rect_ = SDL_Rect{0,0,0,0};
    layout_frame_list();
    ensure_selection_visible();
}

void FrameMovementEditor::synchronize_selection() {
    selected_index_ = clamp_index(selected_index_, static_cast<int>(frames_.size()));
    sync_view_selection_from_actual();
    if (canvas_) canvas_->set_selected_index(selected_index_);
    if (properties_panel_) properties_panel_->refresh_from_selection();
    if (frame_changed_callback_) {
        frame_changed_callback_(selected_index_);
    }
    ensure_selection_visible();
}

void FrameMovementEditor::mark_dirty() {
    sanitize_frames(frames_);
    sync_active_variant_frames();
    dirty_ = true;
    if (canvas_) {
        canvas_->set_frames(frames_, true);
        canvas_->set_selected_index(selected_index_);
    }
    if (totals_panel_) totals_panel_->set_frames(frames_);
    sync_view_selection_from_actual();
    layout_frame_list();
    ensure_selection_visible();

    apply_changes();
    dirty_ = false;
}

void FrameMovementEditor::layout_variant_header() {
    if (variants_.size() != variant_tabs_.size()) {
        variant_tabs_.assign(variants_.size(), VariantTabState{});
    }

    smooth_button_rect_ = SDL_Rect{0, 0, 0, 0};
    if (header_rect_.w <= 0 || header_rect_.h <= 0) {
        add_button_rect_ = SDL_Rect{0, 0, 0, 0};
        return;
    }

    int x = header_rect_.x + kVariantHeaderPadding;
    int y = header_rect_.y + kVariantHeaderPadding;

    for (size_t i = 0; i < variants_.size(); ++i) {
        VariantTabState& tab = variant_tabs_[i];
        tab.rect = SDL_Rect{x, y, kVariantTabWidth, kVariantTabHeight};
        tab.close_visible = !variants_[i].primary;
        if (tab.close_visible) {
            tab.close_rect = SDL_Rect{tab.rect.x + tab.rect.w - kVariantCloseSize - 4,
                                      tab.rect.y + (tab.rect.h - kVariantCloseSize) / 2, kVariantCloseSize, kVariantCloseSize};
        } else {
            tab.close_rect = SDL_Rect{0, 0, 0, 0};
        }
        x += kVariantTabWidth + kVariantTabSpacing;
    }

    add_button_rect_ = SDL_Rect{x, y, kVariantTabHeight, kVariantTabHeight};

    if (false) {
        const int after_add = add_button_rect_.x + add_button_rect_.w + kVariantTabSpacing;
        const int right_edge = header_rect_.x + header_rect_.w - kVariantHeaderPadding;
        int available = std::max(0, right_edge - after_add);
        int smooth_w = 0;
        int show_w = 0;
        int offset_x = right_edge;
        (void)offset_x; (void)available; (void)after_add; (void)right_edge; (void)smooth_w; (void)show_w;
    }
}

void FrameMovementEditor::smooth_frames() {
    const size_t frame_count = frames_.size();
    if (frame_count <= 2) {
        return;
    }

    sanitize_frames(frames_);
    const std::vector<MovementFrame> original_frames = frames_;

    double total_dx = 0.0;
    double total_dy = 0.0;
    for (size_t i = 1; i < frame_count; ++i) {
        const double dx = std::isfinite(frames_[i].dx) ? static_cast<double>(frames_[i].dx) : 0.0;
        const double dy = std::isfinite(frames_[i].dy) ? static_cast<double>(frames_[i].dy) : 0.0;
        total_dx += dx;
        total_dy += dy;
    }

    const size_t steps = frame_count - 1;
    if (steps == 0) {
        return;
    }

    frames_[0].dx = 0.0f;
    frames_[0].dy = 0.0f;

    int accum_x = 0;
    int accum_y = 0;
    for (size_t i = 1; i < frame_count; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(steps);
        const double target_x = total_dx * t;
        const double target_y = total_dy * t;

        int rounded_x = (i == steps) ? static_cast<int>(std::lround(total_dx)) : static_cast<int>(std::lround(target_x));
        int rounded_y = (i == steps) ? static_cast<int>(std::lround(total_dy)) : static_cast<int>(std::lround(target_y));

        const int dx = rounded_x - accum_x;
        const int dy = rounded_y - accum_y;
        accum_x = rounded_x;
        accum_y = rounded_y;

        frames_[i].dx = static_cast<float>(dx);
        frames_[i].dy = static_cast<float>(dy);
    }

    if (!frames_equal(frames_, original_frames)) {
        mark_dirty();
    } else {
        synchronize_selection();
    }
}

void FrameMovementEditor::render_variant_header(SDL_Renderer* renderer) const {
    if (!renderer || header_rect_.w <= 0 || header_rect_.h <= 0) {
        return;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    dm_draw::DrawBeveledRect( renderer, header_rect_, DMStyles::CornerRadius(), DMStyles::BevelDepth(), DMStyles::PanelBG(), DMStyles::HighlightColor(), DMStyles::ShadowColor(), false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());

    const DMButtonStyle& active_style = DMStyles::AccentButton();
    const DMButtonStyle& inactive_style = DMStyles::HeaderButton();

    for (size_t i = 0; i < variants_.size(); ++i) {
        const MovementVariant& variant = variants_[i];
        const VariantTabState& tab = variant_tabs_[i];
        bool is_active = static_cast<int>(i) == active_variant_index_;
        const DMButtonStyle& style = is_active ? active_style : inactive_style;

        SDL_Color button_color = style.bg;
        if (tab.pressed) {
            button_color = style.press_bg;
        } else if (tab.hovered) {
            button_color = style.hover_bg;
        }
        const int tab_radius = std::min(DMStyles::CornerRadius(), std::min(tab.rect.w, tab.rect.h) / 2);
        const int tab_bevel = std::min(DMStyles::BevelDepth(), std::max(0, std::min(tab.rect.w, tab.rect.h) / 2));
        dm_draw::DrawBeveledRect( renderer, tab.rect, tab_radius, tab_bevel, button_color, button_color, button_color, false, 0.0f, 0.0f);
        dm_draw::DrawRoundedOutline( renderer, tab.rect, tab_radius, 1, style.border);

        SDL_Rect text_rect = tab.rect;
        if (tab.close_visible) {
            text_rect.w = std::max(0, tab.close_rect.x - tab.rect.x - 4);
        }
        render_tab_text(renderer, variant.name, text_rect, style.text);

        if (tab.close_visible) {
            SDL_Color close_bg = style.bg;
            if (tab.close_pressed) {
                close_bg = style.press_bg;
            } else if (tab.close_hovered) {
                close_bg = style.hover_bg;
            }
            const int close_radius = std::min(DMStyles::CornerRadius(), std::min(tab.close_rect.w, tab.close_rect.h) / 2);
            const int close_bevel = std::min(DMStyles::BevelDepth(), std::max(0, std::min(tab.close_rect.w, tab.close_rect.h) / 2));
            dm_draw::DrawBeveledRect( renderer, tab.close_rect, close_radius, close_bevel, close_bg, close_bg, close_bg, false, 0.0f, 0.0f);
            dm_draw::DrawRoundedOutline( renderer, tab.close_rect, close_radius, 1, style.border);
            render_tab_text(renderer, "×", tab.close_rect, style.text);
        }
    }

    SDL_Color add_color = add_button_pressed_ ? active_style.press_bg : (add_button_hovered_ ? active_style.hover_bg : active_style.bg);
    const int add_radius = std::min(DMStyles::CornerRadius(), std::min(add_button_rect_.w, add_button_rect_.h) / 2);
    const int add_bevel = std::min(DMStyles::BevelDepth(), std::max(0, std::min(add_button_rect_.w, add_button_rect_.h) / 2));
    dm_draw::DrawBeveledRect( renderer, add_button_rect_, add_radius, add_bevel, add_color, add_color, add_color, false, 0.0f, 0.0f);
    dm_draw::DrawRoundedOutline( renderer, add_button_rect_, add_radius, 1, active_style.border);
    render_tab_text(renderer, "+", add_button_rect_, active_style.text);
}

void FrameMovementEditor::render_frame_list(SDL_Renderer* renderer) const {
    if (!renderer || frame_list_rect_.w <= 0 || frame_list_rect_.h <= 0) {
        return;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    dm_draw::DrawBeveledRect(renderer, frame_list_rect_, DMStyles::CornerRadius(), DMStyles::BevelDepth(), DMStyles::PanelBG(), DMStyles::HighlightColor(), DMStyles::ShadowColor(), false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());

    if (!animation_id_.empty()) {
        SDL_Rect title_rect{ frame_list_rect_.x + kPanelPadding,
                             frame_list_rect_.y + kPanelPadding,
                             std::max(0, frame_list_rect_.w - kPanelPadding * 2), kFrameListTitleHeight };
        render_tab_text(renderer, animation_id_, title_rect, DMStyles::Label().color);
    }

    if (frame_item_rects_.empty()) {
        SDL_Rect empty_rect{ frame_list_rect_.x, frame_list_rect_.y + kPanelPadding + kFrameListTitleHeight,
                             frame_list_rect_.w, std::max(0, frame_list_rect_.h - (kPanelPadding*2 + kFrameListTitleHeight)) };
        render_tab_text(renderer, "No Frames", empty_rect, DMStyles::Label().color);
        return;
    }

    const DMButtonStyle& list_style = DMStyles::ListButton();
    const DMButtonStyle& accent_style = DMStyles::AccentButton();
    SDL_Color text_color = DMStyles::Label().color;
    SDL_Color index_text_color = DMStyles::AccentButton().text;
    const std::string& preview_animation = frame_list_override_animation_id_.empty() ? animation_id_ : frame_list_override_animation_id_;

    for (size_t i = 0; i < frame_item_rects_.size(); ++i) {
        const SDL_Rect& item = frame_item_rects_[i];
        SDL_Color fill = list_style.bg;
        if (static_cast<int>(i) == display_selected_index_) {
            fill = accent_style.hover_bg;
        } else if (static_cast<int>(i) == hovered_frame_index_) {
            fill = accent_style.bg;
        }
        SDL_Color fill_color{fill.r, fill.g, fill.b, 235};
        const int radius = std::min(DMStyles::CornerRadius(), std::min(item.w, item.h) / 2);
        const int bevel = std::min(DMStyles::BevelDepth(), std::max(0, std::min(item.w, item.h) / 2));
        dm_draw::DrawBeveledRect(renderer, item, radius, bevel, fill_color, fill_color, fill_color, false, 0.0f, 0.0f);
        dm_draw::DrawRoundedOutline(renderer, item, radius, 1, list_style.border);

        if (preview_provider_ && !preview_animation.empty()) {
            SDL_Texture* texture = preview_provider_->get_frame_texture(renderer, preview_animation, static_cast<int>(i));
            if (texture) {
                int tex_w = 0;
                int tex_h = 0;
                if (SDL_QueryTexture(texture, nullptr, nullptr, &tex_w, &tex_h) == 0 && tex_w > 0 && tex_h > 0) {
                    const int max_w = std::max(1, item.w - kFrameThumbnailPadding * 2);
                    const int max_h = std::max(1, item.h - kFrameThumbnailPadding * 2);
                    float scale = std::min(static_cast<float>(max_w) / static_cast<float>(tex_w), static_cast<float>(max_h) / static_cast<float>(tex_h));
                    if (scale <= 0.0f) scale = 1.0f;
                    if (scale > 1.0f)  scale = 1.0f;
                    int draw_w = std::max(1, static_cast<int>(std::round(tex_w * scale)));
                    int draw_h = std::max(1, static_cast<int>(std::round(tex_h * scale)));
                    SDL_Rect dst{item.x + (item.w - draw_w) / 2, item.y + (item.h - draw_h) / 2, draw_w, draw_h};
                    SDL_RenderCopy(renderer, texture, nullptr, &dst);
                }
            }
        }

        const int badge_padding = 4;
        const int badge_height = 18;
        const int badge_width = 28;
        SDL_Rect badge{item.x + item.w - badge_width - badge_padding,
                       item.y + item.h - badge_height - badge_padding, badge_width, badge_height};
        SDL_Color badge_bg = DMStyles::PanelBG();
        badge_bg.a = 215;
        const int badge_radius = std::min(DMStyles::CornerRadius(), std::min(badge.w, badge.h) / 2);
        dm_draw::DrawBeveledRect(renderer, badge, badge_radius, 1, badge_bg, badge_bg, badge_bg, false, 0.0f, 0.0f);
        dm_draw::DrawRoundedOutline(renderer, badge, badge_radius, 1, list_style.border);

        render_badge_text_small(renderer, std::to_string(i + 1), badge, index_text_color);
    }

    if (hscroll_track_rect_.w > 0 && hscroll_track_rect_.h > 0) {
        SDL_Color track_bg = DMStyles::PanelBG();
        track_bg.a = 220;
        const int track_radius = std::min(DMStyles::CornerRadius(), std::min(hscroll_track_rect_.w, hscroll_track_rect_.h) / 2);
        dm_draw::DrawBeveledRect(renderer, hscroll_track_rect_, track_radius, 1, track_bg, track_bg, track_bg, false, 0.0f, 0.0f);
        dm_draw::DrawRoundedOutline(renderer, hscroll_track_rect_, track_radius, 1, list_style.border);

        SDL_Color knob_bg = accent_style.bg;
        SDL_Color knob_border = list_style.border;
        const int knob_radius = std::min(DMStyles::CornerRadius(), std::min(hscroll_knob_rect_.w, hscroll_knob_rect_.h) / 2);
        dm_draw::DrawBeveledRect(renderer, hscroll_knob_rect_, knob_radius, 1, knob_bg, knob_bg, knob_bg, false, 0.0f, 0.0f);
        dm_draw::DrawRoundedOutline(renderer, hscroll_knob_rect_, knob_radius, 1, knob_border);
    }

    if (fl_prev_button_rect_.w > 0 && fl_prev_button_rect_.h > 0) {
        const DMButtonStyle& style = DMStyles::AccentButton();
        SDL_Color bg = style.bg;
        if (fl_prev_pressed_) bg = style.press_bg; else if (fl_prev_hovered_) bg = style.hover_bg;
        const int radius = std::min(DMStyles::CornerRadius(), std::min(fl_prev_button_rect_.w, fl_prev_button_rect_.h) / 2);
        dm_draw::DrawBeveledRect(renderer, fl_prev_button_rect_, radius, 1, bg, bg, bg, false, 0.0f, 0.0f);
        dm_draw::DrawRoundedOutline(renderer, fl_prev_button_rect_, radius, 1, style.border);
        render_tab_text(renderer, "<", fl_prev_button_rect_, style.text);
    }
    if (fl_next_button_rect_.w > 0 && fl_next_button_rect_.h > 0) {
        const DMButtonStyle& style = DMStyles::AccentButton();
        SDL_Color bg = style.bg;
        if (fl_next_pressed_) bg = style.press_bg; else if (fl_next_hovered_) bg = style.hover_bg;
        const int radius = std::min(DMStyles::CornerRadius(), std::min(fl_next_button_rect_.w, fl_next_button_rect_.h) / 2);
        dm_draw::DrawBeveledRect(renderer, fl_next_button_rect_, radius, 1, bg, bg, bg, false, 0.0f, 0.0f);
        dm_draw::DrawRoundedOutline(renderer, fl_next_button_rect_, radius, 1, style.border);
        render_tab_text(renderer, ">", fl_next_button_rect_, style.text);
    }
}

void FrameMovementEditor::set_smoothing_enabled(bool enabled) {
    smoothing_enabled_ = enabled;
    if (canvas_) {
        canvas_->set_smoothing_enabled(enabled);
    }
}

void FrameMovementEditor::set_curve_enabled(bool enabled) {
    curve_enabled_ = enabled;
    if (canvas_) {
        canvas_->set_smoothing_curve_enabled(enabled);
    }
}

bool FrameMovementEditor::handle_variant_header_event(const SDL_Event& e) {
    switch (e.type) {
        case SDL_MOUSEMOTION: {
            SDL_Point p{e.motion.x, e.motion.y};
            add_button_hovered_ = SDL_PointInRect(&p, &add_button_rect_) != 0;
            bool consumed = add_button_hovered_;
            for (size_t i = 0; i < variant_tabs_.size(); ++i) {
                VariantTabState& tab = variant_tabs_[i];
                tab.hovered = SDL_PointInRect(&p, &tab.rect) != 0;
                if (tab.close_visible) {
                    tab.close_hovered = SDL_PointInRect(&p, &tab.close_rect) != 0;
                } else {
                    tab.close_hovered = false;
                }
                consumed = consumed || tab.hovered || tab.close_hovered;
            }
            return consumed;
        }
        case SDL_MOUSEBUTTONDOWN: {
            if (e.button.button != SDL_BUTTON_LEFT) {
                return false;
            }
            SDL_Point p{e.button.x, e.button.y};
            if (SDL_PointInRect(&p, &add_button_rect_)) {
                add_button_pressed_ = true;
                return true;
            }
            for (auto& tab : variant_tabs_) {
                if (tab.close_visible && SDL_PointInRect(&p, &tab.close_rect)) {
                    tab.close_pressed = true;
                    return true;
                }
                if (SDL_PointInRect(&p, &tab.rect)) {
                    tab.pressed = true;
                    return true;
                }
            }
            return false;
        }
        case SDL_MOUSEBUTTONUP: {
            if (e.button.button != SDL_BUTTON_LEFT) {
                return false;
            }
            SDL_Point p{e.button.x, e.button.y};
            bool handled = false;
            if (add_button_pressed_) {
                bool inside = SDL_PointInRect(&p, &add_button_rect_) != 0;
                add_button_pressed_ = false;
                if (inside) {
                    add_new_variant();
                    handled = true;
                }
            }
            for (size_t i = 0; i < variant_tabs_.size(); ++i) {
                VariantTabState& tab = variant_tabs_[i];
                if (tab.close_pressed) {
                    bool inside_close = tab.close_visible && SDL_PointInRect(&p, &tab.close_rect) != 0;
                    tab.close_pressed = false;
                    if (inside_close) {
                        delete_variant(static_cast<int>(i));
                        handled = true;
                        break;
                    }
                }
                if (tab.pressed) {
                    bool inside_tab = SDL_PointInRect(&p, &tab.rect) != 0;
                    tab.pressed = false;
                    if (inside_tab) {
                        set_active_variant(static_cast<int>(i), false);
                        handled = true;
                    }
                }
            }
            return handled;
        }
        case SDL_MOUSEWHEEL: {
            int mx = 0, my = 0;
            SDL_GetMouseState(&mx, &my);
            SDL_Point p{mx, my};
            if (SDL_PointInRect(&p, &frame_list_rect_) && hscroll_content_px_ > 0) {
                const int viewport_width = std::max(0, frame_list_rect_.w - kPanelPadding * 2);
                const int step_px = std::max(8, viewport_width / 6);
                int delta = 0;
                if (e.wheel.x != 0) {
                    delta = -e.wheel.x * step_px;
                } else if (e.wheel.y != 0) {
                    delta = -e.wheel.y * step_px;
                }
                if (delta != 0) {
                    const int max_offset = std::max(0, hscroll_content_px_ - viewport_width);
                    hscroll_offset_px_ = std::clamp(hscroll_offset_px_ + delta, 0, max_offset);
                    layout_frame_list();
                    return true;
                }
            }
            break;
        }
        default:
            break;
    }
    return false;
}

bool FrameMovementEditor::handle_frame_list_event(const SDL_Event& e) {
    if (frame_list_rect_.w <= 0 || frame_list_rect_.h <= 0) {
        hovered_frame_index_ = -1;
        return false;
    }

    auto index_at_point = [this](SDL_Point p) {
        for (size_t i = 0; i < frame_item_rects_.size(); ++i) {
            if (SDL_PointInRect(&p, &frame_item_rects_[i])) {
                return static_cast<int>(i);
            }
        }
        return -1;
};

    switch (e.type) {
        case SDL_MOUSEMOTION: {

            if (hscroll_dragging_ && hscroll_track_rect_.w > 0) {
                const int track_x = hscroll_track_rect_.x;
                const int track_w = hscroll_track_rect_.w;
                const int knob_w  = hscroll_knob_rect_.w;
                const int max_offset = std::max(0, hscroll_content_px_ - track_w);
                int desired_knob_x = e.motion.x - hscroll_drag_dx_;
                if (desired_knob_x < track_x) desired_knob_x = track_x;
                if (desired_knob_x > track_x + track_w - knob_w) desired_knob_x = track_x + track_w - knob_w;
                if (track_w - knob_w > 0) {
                    hscroll_offset_px_ = ((desired_knob_x - track_x) * max_offset) / (track_w - knob_w);
                } else {
                    hscroll_offset_px_ = 0;
                }
                layout_frame_list();
                return true;
            }
            SDL_Point p{e.motion.x, e.motion.y};

            fl_prev_hovered_ = SDL_PointInRect(&p, &fl_prev_button_rect_) != 0;
            fl_next_hovered_ = SDL_PointInRect(&p, &fl_next_button_rect_) != 0;
            hovered_frame_index_ = index_at_point(p);
            return SDL_PointInRect(&p, &frame_list_rect_) != 0;
        }
        case SDL_MOUSEBUTTONDOWN: {
            if (e.button.button != SDL_BUTTON_LEFT) {
                break;
            }
            SDL_Point p{e.button.x, e.button.y};

            if (SDL_PointInRect(&p, &fl_prev_button_rect_)) {
                fl_prev_pressed_ = true;
                return true;
            }
            if (SDL_PointInRect(&p, &fl_next_button_rect_)) {
                fl_next_pressed_ = true;
                return true;
            }

            if (hscroll_track_rect_.w > 0 && SDL_PointInRect(&p, &hscroll_knob_rect_)) {
                hscroll_dragging_ = true;
                hscroll_drag_dx_ = p.x - hscroll_knob_rect_.x;
                return true;
            }
            if (hscroll_track_rect_.w > 0 && SDL_PointInRect(&p, &hscroll_track_rect_) && !SDL_PointInRect(&p, &hscroll_knob_rect_)) {
                const int track_x = hscroll_track_rect_.x;
                const int track_w = hscroll_track_rect_.w;
                const int knob_w  = hscroll_knob_rect_.w;
                const int max_offset = std::max(0, hscroll_content_px_ - track_w);
                int desired_knob_x = p.x - knob_w / 2;
                if (desired_knob_x < track_x) desired_knob_x = track_x;
                if (desired_knob_x > track_x + track_w - knob_w) desired_knob_x = track_x + track_w - knob_w;
                if (track_w - knob_w > 0) {
                    hscroll_offset_px_ = ((desired_knob_x - track_x) * max_offset) / (track_w - knob_w);
                } else {
                    hscroll_offset_px_ = 0;
                }
                layout_frame_list();
                return true;
            }
            int index = index_at_point(p);
            if (index >= 0) {
                display_selected_index_ = clamp_view_index(index);
                selected_index_ = map_view_to_actual(display_selected_index_);
                synchronize_selection();
                return true;
            }
            break;
        }
        case SDL_MOUSEBUTTONUP: {
            if (e.button.button != SDL_BUTTON_LEFT) {
                break;
            }
            SDL_Point p{e.button.x, e.button.y};
            if (fl_prev_pressed_) {
                bool inside = SDL_PointInRect(&p, &fl_prev_button_rect_) != 0;
                fl_prev_pressed_ = false;
                if (inside && can_select_previous_frame()) {
                    select_previous_frame();
                    return true;
                }
            }
            if (fl_next_pressed_) {
                bool inside = SDL_PointInRect(&p, &fl_next_button_rect_) != 0;
                fl_next_pressed_ = false;
                if (inside && can_select_next_frame()) {
                    select_next_frame();
                    return true;
                }
            }
            if (hscroll_dragging_) {
                hscroll_dragging_ = false;
                return true;
            }
            if (index_at_point(p) >= 0) {
                return true;
            }
            break;
        }
        default:
            break;
    }

    if (e.type == SDL_MOUSEMOTION) {
        return false;
    }

    if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
        SDL_Point p{e.button.x, e.button.y};
        return SDL_PointInRect(&p, &frame_list_rect_) != 0;
    }

    return false;
}

void FrameMovementEditor::layout_frame_list() {
    frame_item_rects_.clear();
    hovered_frame_index_ = -1;

    const int count = view_frame_count();
    display_selected_index_ = clamp_view_index(display_selected_index_);
    if (hovered_frame_index_ >= count) {
        hovered_frame_index_ = -1;
    }
    if (frame_list_rect_.w <= 0 || frame_list_rect_.h <= 0 || count <= 0) {
        hscroll_content_px_ = 0;
        hscroll_track_rect_ = SDL_Rect{0,0,0,0};
        hscroll_knob_rect_  = SDL_Rect{0,0,0,0};
        return;
    }

    const int padding = kPanelPadding;
    const int spacing = kPanelPadding;
    const int viewport_width = std::max(0, frame_list_rect_.w - padding * 2);
    int available_height = std::max(0, frame_list_rect_.h - padding * 2 - kFrameListTitleHeight);
    if (viewport_width <= 0 || available_height <= 0) {
        return;
    }

    int item_height = std::max(kFrameListMinSize, std::min(std::min(kFrameListMaxSize, available_height), kFrameListBaseSize));
    int item_width  = std::max(kFrameListMinSize, std::min(item_height, kFrameListMaxSize));
    int content_width = (count > 0) ? (count * item_width + (count - 1) * spacing) : 0;
    hscroll_content_px_ = content_width;

    const bool need_scroll = content_width > viewport_width;
    if (need_scroll) {
        available_height = std::max(0, available_height - (kFrameListScrollbarHeight + spacing));
        item_height = std::max(kFrameListMinSize, std::min(item_height, available_height));
        item_width  = std::max(kFrameListMinSize, std::min(item_width,  item_height));
        content_width = (count > 0) ? (count * item_width + (count - 1) * spacing) : 0;
    }

    const int max_offset = std::max(0, content_width - viewport_width);
    if (hscroll_offset_px_ < 0) hscroll_offset_px_ = 0;
    if (hscroll_offset_px_ > max_offset) hscroll_offset_px_ = max_offset;

    int centering_offset = 0;
    if (!need_scroll && viewport_width > content_width) {
        centering_offset = (viewport_width - content_width) / 2;
    }

    const int start_x = frame_list_rect_.x + padding + centering_offset - hscroll_offset_px_;
    const int start_y = frame_list_rect_.y + padding + kFrameListTitleHeight + std::max(0, (available_height - item_height) / 2);

    frame_item_rects_.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        int x = start_x + i * (item_width + spacing);
        SDL_Rect item{x, start_y, item_width, item_height};
        frame_item_rects_.push_back(item);
    }

    if (need_scroll) {
        hscroll_track_rect_ = SDL_Rect{ frame_list_rect_.x + padding,
                                        frame_list_rect_.y + frame_list_rect_.h - padding - kFrameListScrollbarHeight,
                                        viewport_width,
                                        kFrameListScrollbarHeight };
        int knob_w = std::max(kScrollbarMinKnobWidth, (viewport_width * viewport_width) / content_width);
        if (knob_w > viewport_width) knob_w = viewport_width;
        int knob_x = hscroll_track_rect_.x;
        if (max_offset > 0) {
            knob_x = hscroll_track_rect_.x + (hscroll_offset_px_ * (viewport_width - knob_w)) / max_offset;
        }
        hscroll_knob_rect_ = SDL_Rect{ knob_x, hscroll_track_rect_.y, knob_w, hscroll_track_rect_.h };
    } else {
        hscroll_track_rect_ = SDL_Rect{0,0,0,0};
        hscroll_knob_rect_  = SDL_Rect{0,0,0,0};
        hscroll_offset_px_  = 0;
    }

    const int items_area_y = frame_list_rect_.y + padding + kFrameListTitleHeight;
    const int items_area_h = available_height;
    int btn_h = std::max(24, std::min(32, items_area_h));
    int btn_w = btn_h;
    int btn_y = items_area_y + std::max(0, (items_area_h - btn_h) / 2);
    const int nav_pad = 4;
    fl_prev_button_rect_ = SDL_Rect{ frame_list_rect_.x + nav_pad, btn_y, btn_w, btn_h };
    fl_next_button_rect_ = SDL_Rect{ frame_list_rect_.x + frame_list_rect_.w - nav_pad - btn_w, btn_y, btn_w, btn_h };
}

void FrameMovementEditor::set_active_variant(int index, bool preserve_view) {
    if (index < 0 || index >= static_cast<int>(variants_.size())) {
        return;
    }
    if (index == active_variant_index_) {
        return;
    }

    sync_active_variant_frames();
    active_variant_index_ = index;
    frames_ = variants_[active_variant_index_].frames;
    sanitize_frames(frames_);
    selected_index_ = 0;
    update_child_frames(preserve_view);
    layout_variant_header();
    dirty_ = false;
}

void FrameMovementEditor::update_child_frames(bool preserve_view) {
    sync_view_selection_from_actual();
    if (canvas_) {
        canvas_->set_frames(frames_, preserve_view);
        canvas_->set_selected_index(selected_index_);
    }
    if (totals_panel_) {
        totals_panel_->set_frames(frames_);
    }
    if (properties_panel_) {
        properties_panel_->set_frames(&frames_);
        properties_panel_->refresh_from_selection();
    }
    layout_frame_list();
    ensure_selection_visible();
}

void FrameMovementEditor::sync_active_variant_frames() {
    if (active_variant_index_ < 0 || active_variant_index_ >= static_cast<int>(variants_.size())) {
        return;
    }
    variants_[active_variant_index_].frames = frames_;
}

void FrameMovementEditor::add_new_variant() {
    sync_active_variant_frames();

    MovementVariant variant;
    variant.primary = false;
    variant.name = generate_variant_name();
    variant.frames = default_variant_frames();

    variants_.push_back(std::move(variant));
    active_variant_index_ = static_cast<int>(variants_.size() - 1);
    frames_ = variants_.back().frames;
    sanitize_frames(frames_);
    selected_index_ = 0;
    variant_tabs_.resize(variants_.size());
    update_child_frames(false);
    layout_variant_header();
    apply_changes();
    dirty_ = false;
}

void FrameMovementEditor::delete_variant(int index) {
    if (index <= 0 || index >= static_cast<int>(variants_.size())) {
        return;
    }

    variants_.erase(variants_.begin() + index);
    if (variants_.empty()) {
        MovementVariant variant;
        variant.name = "Primary";
        variant.primary = true;
        variant.frames = default_variant_frames();
        variants_.push_back(std::move(variant));
    }

    if (active_variant_index_ >= static_cast<int>(variants_.size())) {
        active_variant_index_ = static_cast<int>(variants_.size()) - 1;
    }
    frames_ = variants_[active_variant_index_].frames;
    sanitize_frames(frames_);
    selected_index_ = 0;
    variant_tabs_.resize(variants_.size());
    update_child_frames(false);
    layout_variant_header();
    apply_changes();
    dirty_ = false;
}

std::string FrameMovementEditor::generate_variant_name() const {
    int suffix = 1;
    while (true) {
        std::string candidate = "Alternative " + std::to_string(suffix);
        bool exists = false;
        for (const auto& variant : variants_) {
            if (variant.name == candidate) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            return candidate;
        }
        ++suffix;
    }
}

void FrameMovementEditor::set_show_animation(bool show) {
    show_animation_ = show;
    if (canvas_) canvas_->set_show_animation_overlay(show_animation_);
}

void FrameMovementEditor::apply_smoothing() {
    smooth_frames();
}

std::pair<int,int> FrameMovementEditor::total_displacement() const {
    int dx = 0, dy = 0;
    for (size_t i = 1; i < frames_.size(); ++i) {
        dx += static_cast<int>(std::lround(frames_[i].dx));
        dy += static_cast<int>(std::lround(frames_[i].dy));
    }
    return {dx, dy};
}

void FrameMovementEditor::set_total_displacement(int target_dx, int target_dy) {
    if (frames_.empty()) return;
    double cur_dx = 0.0;
    double cur_dy = 0.0;
    for (size_t i = 1; i < frames_.size(); ++i) {
        cur_dx += std::isfinite(frames_[i].dx) ? frames_[i].dx : 0.0;
        cur_dy += std::isfinite(frames_[i].dy) ? frames_[i].dy : 0.0;
    }
    const double need_dx = static_cast<double>(target_dx) - cur_dx;
    const double need_dy = static_cast<double>(target_dy) - cur_dy;
    const size_t last = frames_.size() > 0 ? frames_.size() - 1 : 0;
    if (last >= 1) {
        frames_[last].dx = static_cast<float>(std::lround(frames_[last].dx + need_dx));
        frames_[last].dy = static_cast<float>(std::lround(frames_[last].dy + need_dy));
        mark_dirty();
    }
}

}
