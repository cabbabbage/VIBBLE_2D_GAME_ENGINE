#include "AnimationListContextMenu.hpp"

#include <SDL_ttf.h>

#include <algorithm>
#include <utility>

#include "dev_mode/dm_styles.hpp"
#include "dev_mode/draw_utils.hpp"
#include "dev_mode/font_cache.hpp"

namespace animation_editor {

namespace {

int measure_text_width(const DMLabelStyle& style, const std::string& text) {
    if (text.empty()) return 0;
    SDL_Point size = DMFontCache::instance().measure_text(style, text);
    return size.x;
}

SDL_Point event_point(const SDL_Event& e) {
    SDL_Point p{0, 0};
    switch (e.type) {
    case SDL_MOUSEMOTION:
        p.x = e.motion.x;
        p.y = e.motion.y;
        break;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        p.x = e.button.x;
        p.y = e.button.y;
        break;
    case SDL_MOUSEWHEEL:
        SDL_GetMouseState(&p.x, &p.y);
        break;
    default:
        break;
    }
    return p;
}

}

AnimationListContextMenu::AnimationListContextMenu() = default;

void AnimationListContextMenu::open(const SDL_Rect& parent_bounds,
                                    const SDL_Point& anchor,
                                    std::vector<Option> options) {
    options_.clear();
    options_.reserve(options.size());
    for (auto& option : options) {
        if (!option.label.empty()) {
            options_.push_back(std::move(option));
        }
    }

    if (options_.empty()) {
        close();
        return;
    }

    const DMLabelStyle& label_style = DMStyles::Label();
    const int padding_x = DMSpacing::panel_padding() / 2;
    const int padding_y = DMSpacing::small_gap();
    const int opt_height = option_height();

    int width = 0;
    for (const auto& option : options_) {
        width = std::max(width, measure_text_width(label_style, option.label));
    }
    width += padding_x * 2;
    width = std::max(width, 120);

    rect_ = SDL_Rect{anchor.x, anchor.y, width, opt_height * static_cast<int>(options_.size())};

    const int parent_right = parent_bounds.x + parent_bounds.w;
    const int parent_bottom = parent_bounds.y + parent_bounds.h;

    if (rect_.x + rect_.w > parent_right) {
        rect_.x = std::max(parent_bounds.x, parent_right - rect_.w);
    }
    if (rect_.y + rect_.h > parent_bottom) {
        rect_.y = std::max(parent_bounds.y, parent_bottom - rect_.h);
    }
    if (rect_.x < parent_bounds.x) {
        rect_.x = parent_bounds.x;
    }
    if (rect_.y < parent_bounds.y) {
        rect_.y = parent_bounds.y;
    }

    hovered_index_ = -1;
    pressed_index_ = -1;
    open_ = true;
}

void AnimationListContextMenu::close() {
    open_ = false;
    options_.clear();
    hovered_index_ = -1;
    pressed_index_ = -1;
    rect_ = SDL_Rect{0, 0, 0, 0};
}

int AnimationListContextMenu::option_height() const {
    const DMLabelStyle& label_style = DMStyles::Label();
    const int padding_y = DMSpacing::small_gap();
    return label_style.font_size + padding_y * 2;
}

SDL_Rect AnimationListContextMenu::option_rect(int index) const {
    const int opt_height = option_height();
    return SDL_Rect{rect_.x, rect_.y + opt_height * index, rect_.w, opt_height};
}

int AnimationListContextMenu::option_index_at_point(SDL_Point p) const {
    if (!open_ || options_.empty() || !SDL_PointInRect(&p, &rect_)) {
        return -1;
    }
    const int opt_height = option_height();
    int index = (p.y - rect_.y) / opt_height;
    if (index < 0 || index >= static_cast<int>(options_.size())) {
        return -1;
    }
    return index;
}

bool AnimationListContextMenu::handle_event(const SDL_Event& e) {
    if (!open_) return false;

    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
        close();
        return true;
    }

    if (e.type == SDL_MOUSEWHEEL) {
        SDL_Point p = event_point(e);
        if (!SDL_PointInRect(&p, &rect_)) {
            close();
            return false;
        }
        return true;
    }

    if (e.type == SDL_MOUSEMOTION) {
        SDL_Point p = event_point(e);
        hovered_index_ = option_index_at_point(p);
        return hovered_index_ >= 0;
    }

    if (e.type == SDL_MOUSEBUTTONDOWN) {
        SDL_Point p = event_point(e);
        if (!SDL_PointInRect(&p, &rect_)) {
            close();
            return false;
        }
        if (e.button.button == SDL_BUTTON_LEFT) {
            pressed_index_ = option_index_at_point(p);
            return pressed_index_ >= 0;
        }
        return true;
    }

    if (e.type == SDL_MOUSEBUTTONUP) {
        SDL_Point p = event_point(e);
        if (!SDL_PointInRect(&p, &rect_)) {
            close();
            return false;
        }
        if (e.button.button == SDL_BUTTON_LEFT) {
            int index = option_index_at_point(p);
            int pressed = pressed_index_;
            pressed_index_ = -1;
            if (index >= 0 && index < static_cast<int>(options_.size()) && index == pressed) {
                auto callback = options_[index].callback;
                close();
                if (callback) {
                    callback();
                }
                return true;
            }
            return true;
        }
        return true;
    }

    return false;
}

void AnimationListContextMenu::render(SDL_Renderer* renderer) const {
    if (!open_ || !renderer) return;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    dm_draw::DrawBeveledRect(renderer, rect_, DMStyles::CornerRadius(), DMStyles::BevelDepth(), DMStyles::PanelBG(), DMStyles::HighlightColor(), DMStyles::ShadowColor(), false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());

    dm_draw::DrawRoundedOutline(renderer, rect_, DMStyles::CornerRadius(), 1, DMStyles::Border());

    const DMLabelStyle& label_style = DMStyles::Label();
    DMLabelStyle draw_style = label_style;
    const SDL_Color idle_fill = DMStyles::ButtonBaseFill();
    const SDL_Color hover_fill = DMStyles::ButtonHoverFill();
    const SDL_Color press_fill = DMStyles::ButtonPressedFill();

    const int padding_x = DMSpacing::panel_padding() / 2;
    const int padding_y = DMSpacing::small_gap();
    const int opt_height = option_height();

    for (int i = 0; i < static_cast<int>(options_.size()); ++i) {
        SDL_Rect opt_rect = option_rect(i);
        const bool hovered = hovered_index_ == i;
        const bool pressed = pressed_index_ == i;
        const SDL_Color fill = pressed ? press_fill : (hovered ? hover_fill : idle_fill);

        dm_draw::DrawBeveledRect(renderer, opt_rect, DMStyles::CornerRadius(), 0, fill, DMStyles::HighlightColor(), DMStyles::ShadowColor(), false, 0.0f, 0.0f);

        DMFontCache::instance().draw_text(renderer, draw_style, options_[i].label, opt_rect.x + padding_x, opt_rect.y + padding_y);
    }
}

}

