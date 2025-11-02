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
    render_label(renderer, "dX: " + std::to_string(static_cast<int>(std::lround(cached_frame_.dx))), x, y, text_color);
    y += kLineHeight;
    render_label(renderer, "dY: " + std::to_string(static_cast<int>(std::lround(cached_frame_.dy))), x, y, text_color);
    y += kLineHeight;

    SDL_Color toggle_bg = cached_frame_.resort_z ? DMStyles::AccentButton().hover_bg : DMStyles::ListButton().bg;
    const int toggle_radius = std::min(DMStyles::CornerRadius(), std::min(resort_toggle_rect_.w, resort_toggle_rect_.h) / 2);
    const int toggle_bevel = std::min(DMStyles::BevelDepth(), std::max(0, std::min(resort_toggle_rect_.w, resort_toggle_rect_.h) / 2));
    const SDL_Color fill_color{toggle_bg.r, toggle_bg.g, toggle_bg.b, 240};
    dm_draw::DrawBeveledRect( renderer, resort_toggle_rect_, toggle_radius, toggle_bevel, fill_color, fill_color, fill_color, false, 0.0f, 0.0f);
    SDL_Color toggle_border = DMStyles::ListButton().border;
    dm_draw::DrawRoundedOutline( renderer, resort_toggle_rect_, toggle_radius, 1, toggle_border);

    render_label(renderer, cached_frame_.resort_z ? "Resort Z: Yes" : "Resort Z: No", resort_toggle_rect_.x + 8, resort_toggle_rect_.y + 6, text_color);
}

bool FramePropertiesPanel::handle_event(const SDL_Event& e) {
    if (!frames_ || !selected_index_) return false;

    if (point_in_rect(e, resort_toggle_rect_)) {
        cached_frame_.resort_z = !cached_frame_.resort_z;
        apply_to_selected();
        return true;
    }

    if (e.type == SDL_KEYDOWN) {
        if (e.key.keysym.sym == SDLK_r) {
            cached_frame_.resort_z = !cached_frame_.resort_z;
            apply_to_selected();
            return true;
        }
    }
    return false;
}

void FramePropertiesPanel::layout_controls() {
    int width = std::max(0, bounds_.w - 2 * kPadding);
    resort_toggle_rect_ = SDL_Rect{bounds_.x + kPadding,
                                   bounds_.y + kPadding + (kLineHeight + 4) * 4, width, kLineHeight + 8};
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
