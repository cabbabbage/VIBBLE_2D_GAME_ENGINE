#include "TotalsPanel.hpp"

#include <SDL.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <cmath>
#include <string>

#include "MovementCanvas.hpp"
#include "dm_styles.hpp"
#include "draw_utils.hpp"

namespace animation_editor {

namespace {

void render_totals_label(SDL_Renderer* renderer, const std::string& text, int x, int y, SDL_Color color) {
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

}

TotalsPanel::TotalsPanel() = default;

void TotalsPanel::set_bounds(const SDL_Rect& bounds) {
    bounds_ = bounds;
}

void TotalsPanel::set_frames(const std::vector<MovementFrame>& frames) {
    frames_ = frames;
    recalculate_totals();
}

void TotalsPanel::set_selected_index(const int* selected_index) { selected_index_ = selected_index; }

void TotalsPanel::update() {}

void TotalsPanel::render(SDL_Renderer* renderer) const {
    if (!renderer) return;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    dm_draw::DrawBeveledRect( renderer, bounds_, DMStyles::CornerRadius(), DMStyles::BevelDepth(), DMStyles::PanelBG(), DMStyles::HighlightColor(), DMStyles::ShadowColor(), false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());

    SDL_Color button_text = DMStyles::Label().color;

    const int text_padding = 12;
    int text_x = bounds_.x + text_padding;
    int text_y = bounds_.y + text_padding;

    const int frame_count = static_cast<int>(frames_.size());
    int selected = selected_index_ ? *selected_index_ : 0;
    selected = std::clamp(selected, 0, std::max(0, frame_count - 1));

    render_totals_label(renderer, "Frames: " + std::to_string(frame_count), text_x, text_y, button_text);
    text_y += 22;
    render_totals_label(renderer, "Selected: " + std::to_string(selected), text_x, text_y, button_text);
    text_y += 22;
    render_totals_label(renderer, "Total ΔX: " + std::to_string(static_cast<int>(std::lround(total_dx_))), text_x, text_y, button_text);
    text_y += 22;
    render_totals_label(renderer, "Total ΔY: " + std::to_string(static_cast<int>(std::lround(total_dy_))), text_x, text_y, button_text);
}

bool TotalsPanel::handle_event(const SDL_Event& e) {
    return false;
}

void TotalsPanel::recalculate_totals() {
    total_dx_ = 0.0f;
    total_dy_ = 0.0f;
    for (size_t i = 1; i < frames_.size(); ++i) {
        total_dx_ += frames_[i].dx;
        total_dy_ += frames_[i].dy;
    }
}

}

