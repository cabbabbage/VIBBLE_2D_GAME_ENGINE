#include "TotalsPanel.hpp"

#include <SDL.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <cmath>
#include <string>

#include "MovementCanvas.hpp"
#include "dm_styles.hpp"
#include "draw_utils.hpp"
#include "../../../../widgets.hpp"

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

    const int pad = 6;
    const int box_h = DMTextBox::height();
    const int content_x = bounds_.x + pad;
    const int content_y = bounds_.y + pad;
    const int content_w = std::max(0, bounds_.w - pad * 2);
    const int col_w = std::max(0, (content_w - pad) / 2);
    if (!dx_box_) dx_box_ = std::make_unique<DMTextBox>("Total dX", "0");
    if (!dy_box_) dy_box_ = std::make_unique<DMTextBox>("Total dY", "0");
    if (dx_box_) dx_box_->set_rect(SDL_Rect{content_x, content_y, col_w, box_h});
    if (dy_box_) dy_box_->set_rect(SDL_Rect{content_x + col_w + pad, content_y, col_w, box_h});
}

void TotalsPanel::set_frames(const std::vector<MovementFrame>& frames) {
    frames_ = frames;
    recalculate_totals();

    const int dx = static_cast<int>(std::lround(total_dx_));
    const int dy = static_cast<int>(std::lround(total_dy_));
    if (dx_box_ && !dx_box_->is_editing()) dx_box_->set_value(std::to_string(dx));
    if (dy_box_ && !dy_box_->is_editing()) dy_box_->set_value(std::to_string(dy));
}

void TotalsPanel::set_selected_index(const int* selected_index) { selected_index_ = selected_index; }

void TotalsPanel::update() {

}

void TotalsPanel::render(SDL_Renderer* renderer) const {
    if (!renderer) return;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    dm_draw::DrawBeveledRect( renderer, bounds_, DMStyles::CornerRadius(), DMStyles::BevelDepth(), DMStyles::PanelBG(), DMStyles::HighlightColor(), DMStyles::ShadowColor(), false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());

    if (dx_box_) dx_box_->render(renderer);
    if (dy_box_) dy_box_->render(renderer);
}

bool TotalsPanel::handle_event(const SDL_Event& e) {
    bool consumed = false;
    if (dx_box_ && dx_box_->handle_event(e)) {
        consumed = true;

        try {
            int new_dx = std::stoi(dx_box_->value());
            if (on_totals_changed_) on_totals_changed_(new_dx, static_cast<int>(std::lround(total_dy_)));
        } catch (...) {

        }
    }
    if (dy_box_ && dy_box_->handle_event(e)) {
        consumed = true;
        try {
            int new_dy = std::stoi(dy_box_->value());
            if (on_totals_changed_) on_totals_changed_(static_cast<int>(std::lround(total_dx_)), new_dy);
        } catch (...) {
        }
    }
    return consumed;
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

