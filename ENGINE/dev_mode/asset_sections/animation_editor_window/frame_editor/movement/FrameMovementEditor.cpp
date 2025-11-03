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

// Totals panel moved to ToolsPanel; hide internal strip
constexpr int kTotalsHeight = 0;
constexpr int kVariantHeaderPadding = kPanelPadding;
constexpr int kVariantTabHeight = 28;
constexpr int kVariantTabSpacing = 6;
constexpr int kVariantTabWidth = 140;
constexpr int kVariantCloseSize = 18;
constexpr int kFrameListBaseSize = 56;
constexpr int kFrameListMinSize = 36;
constexpr int kFrameThumbnailPadding = 6;

int clamp_index(int index, int max_value) {
    if (max_value <= 0) return 0;
    return std::clamp(index, 0, max_value - 1);
}

void sanitize_frames(std::vector<MovementFrame>& frames) {
    if (frames.empty()) {
        frames.push_back(MovementFrame{});
    }
    if (frames.empty()) return;
    frames.front().dx = 0.0f;
    frames.front().dy = 0.0f;
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
    frames.front().dx = 0.0f;
    frames.front().dy = 0.0f;
    sanitize_frames(frames);
    return frames;
}

nlohmann::json serialize_frames_to_json(const std::vector<MovementFrame>& frames) {
    nlohmann::json movement = nlohmann::json::array();
    for (size_t i = 0; i < frames.size(); ++i) {
        const MovementFrame& frame = frames[i];
        int dx = static_cast<int>(std::lround(i == 0 ? 0.0f : frame.dx));
        int dy = static_cast<int>(std::lround(i == 0 ? 0.0f : frame.dy));
        nlohmann::json entry = nlohmann::json::array({dx, dy});
        if (frame.resort_z) {
            entry.push_back(frame.resort_z);
        }
        movement.push_back(entry);
    }
    if (movement.empty()) {
        movement.push_back(nlohmann::json::array({0, 0}));
    }
    movement[0][0] = 0;
    movement[0][1] = 0;
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

std::vector<MovementFrame> default_variant_frames() {
    return parse_movement_frames(nlohmann::json::array());
}

}

FrameMovementEditor::FrameMovementEditor() { ensure_children(); }

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

void FrameMovementEditor::update() {
    ensure_children();
    if (canvas_) {
        canvas_->update();
        if (selected_index_ != canvas_->selected_index()) {
            selected_index_ = canvas_->selected_index();
            synchronize_selection();
        }
        // Hovering over points in the canvas highlights the corresponding frame in the list
        int hover = canvas_->hovered_index();
        if (hover >= 0 && hover < static_cast<int>(frames_.size())) {
            hovered_frame_index_ = hover;
        }
        // Keep overlay context synchronized
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
    // Smooth and show toggles are managed by ToolsPanel

    if (dirty_) {
        apply_changes();
        dirty_ = false;
    }
}

void FrameMovementEditor::render(SDL_Renderer* renderer) const {
    render_variant_header(renderer);
    // No local controls rendered here (moved to ToolsPanel)
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
    if (frames_.empty()) return false;
    return selected_index_ > 0;
}

bool FrameMovementEditor::can_select_next_frame() const {
    if (frames_.empty()) return false;
    return selected_index_ < static_cast<int>(frames_.size()) - 1;
}

void FrameMovementEditor::select_previous_frame() {
    selected_index_ = clamp_index(selected_index_, static_cast<int>(frames_.size()));
    if (selected_index_ <= 0) return;
    --selected_index_;
    synchronize_selection();
}

void FrameMovementEditor::select_next_frame() {
    selected_index_ = clamp_index(selected_index_, static_cast<int>(frames_.size()));
    if (selected_index_ >= static_cast<int>(frames_.size()) - 1) return;
    ++selected_index_;
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
    if (totals_panel_) totals_panel_->set_frames(frames_);
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
    // Controls moved to external tools panel
    // Remove frame properties panel entirely (requested)
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
        // Compact single-column: totals only
        const int totals_height = std::min(content_h, kTotalsHeight);
        totals_rect_ = SDL_Rect{content_x, content_y, content_w, totals_height};
        properties_rect_ = SDL_Rect{0, 0, 0, 0};
    }

    if (totals_panel_) totals_panel_->set_bounds(totals_rect_);
    // properties panel removed

    layout_variant_header();
    // Local control rects cleared; ToolsPanel owns controls
    smooth_button_rect_ = SDL_Rect{0,0,0,0};
    show_anim_button_rect_ = SDL_Rect{0,0,0,0};
    layout_frame_list();
}

void FrameMovementEditor::synchronize_selection() {
    selected_index_ = clamp_index(selected_index_, static_cast<int>(frames_.size()));
    if (canvas_) canvas_->set_selected_index(selected_index_);
    if (properties_panel_) properties_panel_->refresh_from_selection();
    if (frame_changed_callback_) {
        frame_changed_callback_(selected_index_);
    }
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
    layout_frame_list();
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

        int rounded_x = (i == steps) ? static_cast<int>(std::lround(total_dx))
                                     : static_cast<int>(std::lround(target_x));
        int rounded_y = (i == steps) ? static_cast<int>(std::lround(total_dy))
                                     : static_cast<int>(std::lround(target_y));

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
    dm_draw::DrawBeveledRect(renderer, frame_list_rect_, DMStyles::CornerRadius(), DMStyles::BevelDepth(), DMStyles::PanelBG(),
                             DMStyles::HighlightColor(), DMStyles::ShadowColor(), false, DMStyles::HighlightIntensity(),
                             DMStyles::ShadowIntensity());

    if (frame_item_rects_.empty()) {
        render_tab_text(renderer, "No Frames", frame_list_rect_, DMStyles::Label().color);
        return;
    }

    const DMButtonStyle& list_style = DMStyles::ListButton();
    const DMButtonStyle& accent_style = DMStyles::AccentButton();
    SDL_Color text_color = DMStyles::Label().color;

    for (size_t i = 0; i < frame_item_rects_.size(); ++i) {
        const SDL_Rect& item = frame_item_rects_[i];
        SDL_Color fill = list_style.bg;
        if (static_cast<int>(i) == selected_index_) {
            fill = accent_style.hover_bg;
        } else if (static_cast<int>(i) == hovered_frame_index_) {
            fill = accent_style.bg;
        }
        SDL_Color fill_color{fill.r, fill.g, fill.b, 235};
        const int radius = std::min(DMStyles::CornerRadius(), std::min(item.w, item.h) / 2);
        const int bevel = std::min(DMStyles::BevelDepth(), std::max(0, std::min(item.w, item.h) / 2));
        dm_draw::DrawBeveledRect(renderer, item, radius, bevel, fill_color, fill_color, fill_color, false, 0.0f, 0.0f);
        dm_draw::DrawRoundedOutline(renderer, item, radius, 1, list_style.border);

        bool rendered_texture = false;
        if (preview_provider_ && !animation_id_.empty()) {
            SDL_Texture* texture = preview_provider_->get_frame_texture(renderer, animation_id_, static_cast<int>(i));
            if (texture) {
                int tex_w = 0;
                int tex_h = 0;
                if (SDL_QueryTexture(texture, nullptr, nullptr, &tex_w, &tex_h) == 0 && tex_w > 0 && tex_h > 0) {
                    const int max_w = std::max(1, item.w - kFrameThumbnailPadding * 2);
                    const int max_h = std::max(1, item.h - kFrameThumbnailPadding * 2);
                    float scale = std::min(static_cast<float>(max_w) / static_cast<float>(tex_w),
                                            static_cast<float>(max_h) / static_cast<float>(tex_h));
                    if (scale <= 0.0f) {
                        scale = 1.0f;
                    }
                    if (scale > 1.0f) {
                        scale = 1.0f;
                    }
                    int draw_w = std::max(1, static_cast<int>(std::round(tex_w * scale)));
                    int draw_h = std::max(1, static_cast<int>(std::round(tex_h * scale)));
                    SDL_Rect dst{item.x + (item.w - draw_w) / 2, item.y + (item.h - draw_h) / 2, draw_w, draw_h};
                    SDL_RenderCopy(renderer, texture, nullptr, &dst);
                    rendered_texture = true;
                }
            }
        }

        if (!rendered_texture) {
            render_tab_text(renderer, std::to_string(i + 1), item, text_color);
        } else {
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
            render_tab_text(renderer, std::to_string(i + 1), badge, text_color);
        }
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
            SDL_Point p{e.motion.x, e.motion.y};
            hovered_frame_index_ = index_at_point(p);
            return SDL_PointInRect(&p, &frame_list_rect_) != 0;
        }
        case SDL_MOUSEBUTTONDOWN: {
            if (e.button.button != SDL_BUTTON_LEFT) {
                break;
            }
            SDL_Point p{e.button.x, e.button.y};
            int index = index_at_point(p);
            if (index >= 0) {
                selected_index_ = clamp_index(index, static_cast<int>(frames_.size()));
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

    if (frame_list_rect_.w <= 0 || frame_list_rect_.h <= 0 || frames_.empty()) {
        return;
    }

    const int padding = kPanelPadding;
    const int spacing = kPanelPadding;
    const int available_width = std::max(0, frame_list_rect_.w - padding * 2);
    const int available_height = std::max(0, frame_list_rect_.h - padding * 2);
    if (available_width <= 0 || available_height <= 0) {
        return;
    }

    int columns = std::max(1, std::min(static_cast<int>(frames_.size()), available_width / (kFrameListMinSize + spacing)));
    if (columns == 0) columns = 1;
    int rows = std::max(1, static_cast<int>((frames_.size() + columns - 1) / columns));

    int item_width = std::max(kFrameListMinSize,
                              std::min(kFrameListBaseSize, (available_width - spacing * (columns - 1)) / columns));
    int item_height = std::max(kFrameListMinSize,
                               std::min(kFrameListBaseSize, (available_height - spacing * (rows - 1)) / rows));

    int used_width = columns * item_width + (columns - 1) * spacing;
    int used_height = rows * item_height + (rows - 1) * spacing;

    int start_x = frame_list_rect_.x + padding + std::max(0, (available_width - used_width) / 2);
    int start_y = frame_list_rect_.y + padding + std::max(0, (available_height - used_height) / 2);

    frame_item_rects_.reserve(frames_.size());
    int index = 0;
    for (int row = 0; row < rows && index < static_cast<int>(frames_.size()); ++row) {
        for (int col = 0; col < columns && index < static_cast<int>(frames_.size()); ++col) {
            SDL_Rect item{start_x + col * (item_width + spacing), start_y + row * (item_height + spacing), item_width,
                          item_height};
            frame_item_rects_.push_back(item);
            ++index;
        }
    }
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
