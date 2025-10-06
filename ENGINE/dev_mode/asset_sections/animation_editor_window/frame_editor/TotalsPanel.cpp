#include "TotalsPanel.hpp"

#include <SDL.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <cmath>
#include <string>

#include "MovementCanvas.hpp"
#include "dm_styles.hpp"

namespace animation_editor {

namespace {

const int kButtonSize = 28;
const int kPadding = 12;

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

}  // namespace

TotalsPanel::TotalsPanel() = default;

void TotalsPanel::set_bounds(const SDL_Rect& bounds) {
    bounds_ = bounds;
    prev_button_ = SDL_Rect{bounds_.x + kPadding, bounds_.y + kPadding, kButtonSize, kButtonSize};
    next_button_ = SDL_Rect{prev_button_.x + kButtonSize + kPadding / 2, prev_button_.y, kButtonSize, kButtonSize};
}

void TotalsPanel::set_frames(const std::vector<MovementFrame>& frames) {
    frames_ = frames;
    recalculate_totals();
}

void TotalsPanel::set_selected_index(int* selected_index) { selected_index_ = selected_index; }

void TotalsPanel::set_on_selection_changed(std::function<void(int)> callback) {
    on_selection_changed_ = std::move(callback);
}

void TotalsPanel::update() {}

void TotalsPanel::render(SDL_Renderer* renderer) const {
    if (!renderer) return;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_Color bg = DMStyles::PanelBG();
    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, 220);
    SDL_RenderFillRect(renderer, &bounds_);

    SDL_Color border = DMStyles::AccentButton().hover_bg;
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, 255);
    SDL_RenderDrawRect(renderer, &bounds_);

    SDL_Color button_bg = DMStyles::ListButton().normal_bg;
    SDL_Color button_border = DMStyles::ListButton().border;
    SDL_Color button_text = DMStyles::Label().color;

    SDL_SetRenderDrawColor(renderer, button_bg.r, button_bg.g, button_bg.b, 240);
    SDL_RenderFillRect(renderer, &prev_button_);
    SDL_RenderFillRect(renderer, &next_button_);
    SDL_SetRenderDrawColor(renderer, button_border.r, button_border.g, button_border.b, 255);
    SDL_RenderDrawRect(renderer, &prev_button_);
    SDL_RenderDrawRect(renderer, &next_button_);

    render_label(renderer, "<", prev_button_.x + (prev_button_.w / 2) - 6,
                 prev_button_.y + (prev_button_.h / 2) - 10, button_text);
    render_label(renderer, ">", next_button_.x + (next_button_.w / 2) - 6,
                 next_button_.y + (next_button_.h / 2) - 10, button_text);

    int text_x = next_button_.x + next_button_.w + kPadding;
    int text_y = bounds_.y + kPadding;

    const int frame_count = static_cast<int>(frames_.size());
    int selected = selected_index_ ? *selected_index_ : 0;
    selected = std::clamp(selected, 0, std::max(0, frame_count - 1));

    render_label(renderer, "Frames: " + std::to_string(frame_count), text_x, text_y, button_text);
    text_y += 22;
    render_label(renderer, "Selected: " + std::to_string(selected), text_x, text_y, button_text);
    text_y += 22;
    render_label(renderer, "Total ΔX: " + std::to_string(static_cast<int>(std::lround(total_dx_))), text_x, text_y,
                 button_text);
    text_y += 22;
    render_label(renderer, "Total ΔY: " + std::to_string(static_cast<int>(std::lround(total_dy_))), text_x, text_y,
                 button_text);
}

bool TotalsPanel::handle_event(const SDL_Event& e) {
    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        if (point_in_rect(e, prev_button_) && selected_index_ && *selected_index_ > 0) {
            --(*selected_index_);
            if (on_selection_changed_) on_selection_changed_(*selected_index_);
            return true;
        }
        if (point_in_rect(e, next_button_) && selected_index_) {
            const int last_index = std::max(0, static_cast<int>(frames_.size()) - 1);
            if (*selected_index_ < last_index) {
                ++(*selected_index_);
                if (on_selection_changed_) on_selection_changed_(*selected_index_);
            }
            return true;
        }
    }
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

}  // namespace animation_editor

