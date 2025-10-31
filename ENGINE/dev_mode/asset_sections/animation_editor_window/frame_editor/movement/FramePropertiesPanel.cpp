#include "FramePropertiesPanel.hpp"

#include <SDL_ttf.h>

#include <algorithm>
#include <cmath>
#include <string>

#include "MovementCanvas.hpp"
#include "dm_styles.hpp"
#include "draw_utils.hpp"

namespace animation_editor {

namespace {

const int kPadding = 12;
const int kLineHeight = 22;

void render_label(SDL_Renderer* renderer, const std::string& text, int x, int y, SDL_Color color) {
    if (!renderer || text.empty()) return;
    const DMLabelStyle& style = DMStyles::Label();
    TTF_Font* font = style.open_font();
    if (!font) return;
    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text.c_str(), color);
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

bool point_in_rect(const SDL_Event& e, const SDL_Rect& rect) {
    if (e.type != SDL_MOUSEBUTTONDOWN) return false;
    SDL_Point p{e.button.x, e.button.y};
    return SDL_PointInRect(&p, &rect) != 0;
}

}

FramePropertiesPanel::FramePropertiesPanel() = default;

void FramePropertiesPanel::set_bounds(const SDL_Rect& bounds) {
    bounds_ = bounds;
    layout_controls();
}

void FramePropertiesPanel::set_frames(std::vector<MovementFrame>* frames) {
    frames_ = frames;
    sync_from_selected();
}

void FramePropertiesPanel::set_selected_index(int* selected_index) {
    selected_index_ = selected_index;
    sync_from_selected();
}

void FramePropertiesPanel::set_canvas(MovementCanvas* canvas) {
    canvas_ = canvas;
}

void FramePropertiesPanel::set_on_frame_changed(std::function<void()> callback) {
    on_frame_changed_ = std::move(callback);
}

void FramePropertiesPanel::refresh_from_selection() { sync_from_selected(); }

bool FramePropertiesPanel::take_dirty_flag() {
    if (!dirty_) return false;
    dirty_ = false;
    return true;
}

void FramePropertiesPanel::update() {
    if (!selected_index_ || !frames_) return;
    int index = std::clamp(*selected_index_, 0, static_cast<int>(frames_->size()) - 1);
    if (index != cached_index_) {
        sync_from_selected();
    }
}

void FramePropertiesPanel::render(SDL_Renderer* renderer) const {
    if (!renderer) return;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    dm_draw::DrawBeveledRect( renderer, bounds_, DMStyles::CornerRadius(), DMStyles::BevelDepth(), DMStyles::PanelBG(), DMStyles::HighlightColor(), DMStyles::ShadowColor(), false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());

    SDL_Color text_color = DMStyles::Label().color;
    int x = bounds_.x + kPadding;
    int y = bounds_.y + kPadding;

    render_label(renderer, "Frame Properties", x, y, text_color);
    y += kLineHeight + 4;

    render_label(renderer, "Index: " + std::to_string(std::max(0, cached_index_)), x, y, text_color);
    y += kLineHeight;
    render_label(renderer, "ΔX: " + std::to_string(static_cast<int>(std::lround(cached_frame_.dx))), x, y, text_color);
    y += kLineHeight;
    render_label(renderer, "ΔY: " + std::to_string(static_cast<int>(std::lround(cached_frame_.dy))), x, y, text_color);
    y += kLineHeight;

    SDL_Color toggle_bg = cached_frame_.resort_z ? DMStyles::AccentButton().hover_bg : DMStyles::ListButton().bg;
    const int toggle_radius = std::min(DMStyles::CornerRadius(), std::min(resort_toggle_rect_.w, resort_toggle_rect_.h) / 2);
    const int toggle_bevel = std::min(DMStyles::BevelDepth(), std::max(0, std::min(resort_toggle_rect_.w, resort_toggle_rect_.h) / 2));
    const SDL_Color fill_color{toggle_bg.r, toggle_bg.g, toggle_bg.b, 240};
    dm_draw::DrawBeveledRect( renderer, resort_toggle_rect_, toggle_radius, toggle_bevel, fill_color, fill_color, fill_color, false, 0.0f, 0.0f);
    SDL_Color toggle_border = DMStyles::ListButton().border;
    dm_draw::DrawRoundedOutline( renderer, resort_toggle_rect_, toggle_radius, 1, toggle_border);

    render_label(renderer, cached_frame_.resort_z ? "Resort Z: Yes" : "Resort Z: No", resort_toggle_rect_.x + 8, resort_toggle_rect_.y + 6, text_color);

    y = resort_toggle_rect_.y + resort_toggle_rect_.h + kLineHeight / 2;
    render_label(renderer, "Grid Snap:", x, y, text_color);

    // Grid controls
    if (grid_down_rect_.w > 0 && grid_down_rect_.h > 0) {
        SDL_Color button_bg = grid_down_pressed_ ? DMStyles::AccentButton().press_bg :
                          (grid_down_hovered_ ? DMStyles::AccentButton().hover_bg : DMStyles::AccentButton().bg);
        const int button_radius = std::min(DMStyles::CornerRadius(), std::min(grid_down_rect_.w, grid_down_rect_.h) / 2);
        const int button_bevel = std::min(DMStyles::BevelDepth(), std::max(0, std::min(grid_down_rect_.w, grid_down_rect_.h) / 2));
        dm_draw::DrawBeveledRect(renderer, grid_down_rect_, button_radius, button_bevel, button_bg, button_bg, button_bg, false, 0.0f, 0.0f);
        dm_draw::DrawRoundedOutline(renderer, grid_down_rect_, button_radius, 1, DMStyles::AccentButton().border);
        render_label(renderer, "-", grid_down_rect_.x + grid_down_rect_.w/2 - 4, grid_down_rect_.y + grid_down_rect_.h/2 - 6, DMStyles::AccentButton().text);
    }

    if (grid_display_rect_.w > 0 && grid_display_rect_.h > 0) {
        SDL_Color display_bg = DMStyles::PanelBG();
        const int display_radius = std::min(DMStyles::CornerRadius(), std::min(grid_display_rect_.w, grid_display_rect_.h) / 2);
        const int display_bevel = std::min(DMStyles::BevelDepth(), std::max(0, std::min(grid_display_rect_.w, grid_display_rect_.h) / 2));
        dm_draw::DrawBeveledRect(renderer, grid_display_rect_, display_radius, display_bevel, display_bg, display_bg, display_bg, false, 0.0f, 0.0f);
        dm_draw::DrawRoundedOutline(renderer, grid_display_rect_, display_radius, 1, DMStyles::ListButton().border);

        if (canvas_) {
            std::string grid_text = std::to_string(static_cast<int>(canvas_->grid_resolution() * 10) / 10.0f);
            render_label(renderer, grid_text, grid_display_rect_.x + grid_display_rect_.w/2 - 10, grid_display_rect_.y + grid_display_rect_.h/2 - 6, text_color);
        }
    }

    if (grid_up_rect_.w > 0 && grid_up_rect_.h > 0) {
        SDL_Color button_bg = grid_up_pressed_ ? DMStyles::AccentButton().press_bg :
                        (grid_up_hovered_ ? DMStyles::AccentButton().hover_bg : DMStyles::AccentButton().bg);
        const int button_radius = std::min(DMStyles::CornerRadius(), std::min(grid_up_rect_.w, grid_up_rect_.h) / 2);
        const int button_bevel = std::min(DMStyles::BevelDepth(), std::max(0, std::min(grid_up_rect_.w, grid_up_rect_.h) / 2));
        dm_draw::DrawBeveledRect(renderer, grid_up_rect_, button_radius, button_bevel, button_bg, button_bg, button_bg, false, 0.0f, 0.0f);
        dm_draw::DrawRoundedOutline(renderer, grid_up_rect_, button_radius, 1, DMStyles::AccentButton().border);
        render_label(renderer, "+", grid_up_rect_.x + grid_up_rect_.w/2 - 4, grid_up_rect_.y + grid_up_rect_.h/2 - 6, DMStyles::AccentButton().text);
    }
}

bool FramePropertiesPanel::handle_event(const SDL_Event& e) {
    if (!frames_ || !selected_index_) return false;

    if (point_in_rect(e, resort_toggle_rect_)) {
        cached_frame_.resort_z = !cached_frame_.resort_z;
        apply_to_selected();
        return true;
    }

    // Handle grid control buttons
    if (point_in_rect(e, grid_down_rect_) && canvas_) {
        float current = canvas_->grid_resolution();
        canvas_->set_grid_resolution(std::max(0.1f, current - 0.1f));
        return true;
    }

    if (point_in_rect(e, grid_up_rect_) && canvas_) {
        float current = canvas_->grid_resolution();
        canvas_->set_grid_resolution(current + 0.1f);
        return true;
    }

    // Handle hover states for grid buttons
    if (e.type == SDL_MOUSEMOTION && (grid_down_rect_.w > 0 || grid_up_rect_.w > 0)) {
        SDL_Point p{e.motion.x, e.motion.y};
        grid_down_hovered_ = SDL_PointInRect(&p, &grid_down_rect_);
        grid_up_hovered_ = SDL_PointInRect(&p, &grid_up_rect_);
        return (grid_down_hovered_ || grid_up_hovered_) && SDL_PointInRect(&p, &bounds_);
    }

    // Handle button press states
    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        SDL_Point p{e.button.x, e.button.y};
        if (SDL_PointInRect(&p, &grid_down_rect_)) {
            grid_down_pressed_ = true;
            return true;
        }
        if (SDL_PointInRect(&p, &grid_up_rect_)) {
            grid_up_pressed_ = true;
            return true;
        }
    }

    if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
        bool was_pressed = grid_down_pressed_ || grid_up_pressed_;
        grid_down_pressed_ = false;
        grid_up_pressed_ = false;
        if (was_pressed) return true;
    }

    if (e.type == SDL_KEYDOWN) {
        if (e.key.keysym.sym == SDLK_r) {
            cached_frame_.resort_z = !cached_frame_.resort_z;
            apply_to_selected();
            return true;
        }
        // Grid snap shortcuts
        if (canvas_ && SDL_GetModState() & KMOD_CTRL) {
            if (e.key.keysym.sym == SDLK_EQUALS || e.key.keysym.sym == SDLK_KP_PLUS) {
                float current = canvas_->grid_resolution();
                canvas_->set_grid_resolution(current + 0.1f);
                return true;
            }
            if (e.key.keysym.sym == SDLK_MINUS || e.key.keysym.sym == SDLK_KP_MINUS) {
                float current = canvas_->grid_resolution();
                canvas_->set_grid_resolution(std::max(0.1f, current - 0.1f));
                return true;
            }
        }
    }
    return false;
}

void FramePropertiesPanel::layout_controls() {
    int width = std::max(0, bounds_.w - 2 * kPadding);
    resort_toggle_rect_ = SDL_Rect{bounds_.x + kPadding,
                                   bounds_.y + kPadding + (kLineHeight + 4) * 4, width, kLineHeight + 8};

    // Grid controls after the label "Grid Snap:"
    int grid_y = resort_toggle_rect_.y + resort_toggle_rect_.h + kLineHeight + kLineHeight / 2;
    int button_size = kLineHeight + 8;
    int spacing = 4;

    if (bounds_.w >= 3 * button_size + 2 * spacing + 2 * kPadding) {
        // All three controls fit
        grid_down_rect_ = SDL_Rect{bounds_.x + kPadding, grid_y, button_size, button_size};
        grid_display_rect_ = SDL_Rect{bounds_.x + kPadding + button_size + spacing, grid_y, button_size, button_size};
        grid_up_rect_ = SDL_Rect{bounds_.x + kPadding + 2 * button_size + 2 * spacing, grid_y, button_size, button_size};
    } else {
        // Minimal layout - just display
        grid_down_rect_ = SDL_Rect{0, 0, 0, 0};
        grid_display_rect_ = SDL_Rect{bounds_.x + kPadding, grid_y, button_size, button_size};
        grid_up_rect_ = SDL_Rect{0, 0, 0, 0};
    }
}

void FramePropertiesPanel::sync_from_selected() {
    if (!frames_ || !selected_index_ || frames_->empty()) {
        cached_frame_ = MovementFrame{};
        cached_index_ = -1;
        return;
    }
    int index = std::clamp(*selected_index_, 0, static_cast<int>(frames_->size()) - 1);
    cached_index_ = index;
    cached_frame_ = (*frames_)[static_cast<size_t>(index)];
}

void FramePropertiesPanel::apply_to_selected() {
    if (!frames_ || !selected_index_) return;
    int index = std::clamp(*selected_index_, 0, static_cast<int>(frames_->size()) - 1);
    (*frames_)[static_cast<size_t>(index)].resort_z = cached_frame_.resort_z;
    dirty_ = true;
    if (on_frame_changed_) on_frame_changed_();
}

}
