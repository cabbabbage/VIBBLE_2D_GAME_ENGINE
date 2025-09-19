#include "full_screen_collapsible.hpp"

#include "utils/input.hpp"

#include <SDL_ttf.h>

#include <algorithm>

namespace {
constexpr int kHeaderHeight = 40;
constexpr int kArrowButtonWidth = 36;

void draw_label(SDL_Renderer* renderer, const std::string& text, int x, int y) {
    if (!renderer) return;
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

} // namespace

FullScreenCollapsible::FullScreenCollapsible(std::string title)
    : title_(std::move(title)) {
    arrow_button_ = std::make_unique<DMButton>("▲", &DMStyles::HeaderButton(), kArrowButtonWidth, DMButton::height());
}

void FullScreenCollapsible::set_bounds(int width, int height) {
    screen_w_ = width;
    screen_h_ = height;
    layout();
}

void FullScreenCollapsible::update_title_width() {
    title_width_ = 0;
    const DMLabelStyle& style = DMStyles::Label();
    TTF_Font* font = style.open_font();
    if (!font) return;
    int w = 0;
    int h = 0;
    if (TTF_SizeUTF8(font, title_.c_str(), &w, &h) == 0) {
        title_width_ = w;
    }
    TTF_CloseFont(font);
}

void FullScreenCollapsible::set_expanded(bool expanded) {
    if (expanded_ == expanded) return;
    expanded_ = expanded;
    if (arrow_button_) {
        arrow_button_->set_text(expanded_ ? "▼" : "▲");
        arrow_button_->set_rect(SDL_Rect{header_rect_.x + header_rect_.w - kArrowButtonWidth - DMSpacing::item_gap(),
                                         header_rect_.y + (kHeaderHeight - DMButton::height()) / 2,
                                         kArrowButtonWidth,
                                         DMButton::height()});
    }
    layout();
    if (on_toggle_) on_toggle_(expanded_);
}

void FullScreenCollapsible::set_header_buttons(std::vector<HeaderButton> buttons) {
    buttons_ = std::move(buttons);
    for (auto& btn : buttons_) {
        btn.widget = std::make_unique<DMButton>(btn.label, &DMStyles::HeaderButton(), 120, DMButton::height());
    }
    layout_buttons();
}

void FullScreenCollapsible::activate_button(const std::string& id) {
    for (auto& btn : buttons_) {
        const bool new_state = (btn.id == id);
        if (btn.active != new_state) {
            btn.active = new_state;
            if (btn.on_toggle) {
                btn.on_toggle(btn.active);
            }
        }
    }
}

void FullScreenCollapsible::set_active_button(const std::string& id, bool trigger_callback) {
    for (auto& btn : buttons_) {
        const bool new_state = (!id.empty() && btn.id == id);
        if (btn.active != new_state) {
            btn.active = new_state;
            if (trigger_callback && btn.on_toggle) {
                btn.on_toggle(btn.active);
            }
        }
    }
}

void FullScreenCollapsible::update(const Input&) {
    // Header buttons rely on DMButton's internal hover state which is updated
    // by handle_event(). Nothing to do here yet.
}

bool FullScreenCollapsible::handle_event(const SDL_Event& e) {
    if (!visible_) return false;
    bool used = false;
    if (arrow_button_) {
        if (arrow_button_->handle_event(e)) {
            used = true;
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                set_expanded(!expanded_);
            }
        }
    }
    for (auto& btn : buttons_) {
        if (!btn.widget) continue;
        if (btn.widget->handle_event(e)) {
            used = true;
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                if (btn.momentary) {
                    if (btn.on_toggle) btn.on_toggle(true);
                    btn.active = false;
                } else {
                    if (btn.active) {
                        btn.active = false;
                        if (btn.on_toggle) btn.on_toggle(false);
                    } else {
                        set_active_button(btn.id, true);
                    }
                }
            }
        }
    }

    if (expanded_ && content_event_handler_) {
        if (content_event_handler_(e)) {
            used = true;
        }
    }

    if (used) return true;

    const bool pointer_event =
        (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEMOTION);
    if (pointer_event) {
        SDL_Point p{
            e.type == SDL_MOUSEMOTION ? e.motion.x : e.button.x,
            e.type == SDL_MOUSEMOTION ? e.motion.y : e.button.y
        };
        if (SDL_PointInRect(&p, &header_rect_)) {
            return true;
        }
        if (expanded_ && SDL_PointInRect(&p, &content_rect_)) {
            // Allow callers to route events to embedded widgets while keeping
            // containment checks for input blocking elsewhere.
            return false;
        }
    } else if (e.type == SDL_MOUSEWHEEL) {
        int mx = 0;
        int my = 0;
        SDL_GetMouseState(&mx, &my);
        SDL_Point p{mx, my};
        if (SDL_PointInRect(&p, &header_rect_)) {
            return true;
        }
        if (expanded_ && SDL_PointInRect(&p, &content_rect_)) {
            return false;
        }
    }
    return false;
}

void FullScreenCollapsible::render(SDL_Renderer* renderer) const {
    if (!visible_ || !renderer) return;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    const SDL_Color header_bg = DMStyles::PanelBG();
    SDL_SetRenderDrawColor(renderer, header_bg.r, header_bg.g, header_bg.b, 240);
    SDL_RenderFillRect(renderer, &header_rect_);
    const SDL_Color border = DMStyles::Border();
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &header_rect_);

    if (expanded_) {
        SDL_Rect content = content_rect_;
        SDL_SetRenderDrawColor(renderer, header_bg.r, header_bg.g, header_bg.b, 220);
        SDL_RenderFillRect(renderer, &content);
        SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
        SDL_RenderDrawRect(renderer, &content);
    }

    int text_x = header_rect_.x + DMSpacing::item_gap();
    int text_y = header_rect_.y + (kHeaderHeight - DMStyles::Label().font_size) / 2;
    draw_label(renderer, title_, text_x, text_y);

    for (const auto& btn : buttons_) {
        if (!btn.widget) continue;
        if (btn.active) {
            SDL_Rect rect = btn.widget->rect();
            SDL_SetRenderDrawColor(renderer, 120, 160, 255, 80);
            SDL_RenderFillRect(renderer, &rect);
        }
        btn.widget->render(renderer);
    }

    if (arrow_button_) {
        arrow_button_->render(renderer);
    }
}

void FullScreenCollapsible::layout() {
    header_rect_.w = screen_w_;
    header_rect_.h = kHeaderHeight;
    header_rect_.x = 0;
    header_rect_.y = expanded_ ? 0 : std::max(0, screen_h_ - kHeaderHeight);
    if (expanded_) {
        content_rect_.x = 0;
        content_rect_.y = header_rect_.y + header_rect_.h;
        content_rect_.w = screen_w_;
        content_rect_.h = std::max(0, screen_h_ - header_rect_.h);
    } else {
        content_rect_ = SDL_Rect{0, header_rect_.y + header_rect_.h, screen_w_, 0};
    }
    update_title_width();
    layout_buttons();
    if (arrow_button_) {
        SDL_Rect rect{header_rect_.x + header_rect_.w - kArrowButtonWidth - DMSpacing::item_gap(),
                      header_rect_.y + (kHeaderHeight - DMButton::height()) / 2,
                      kArrowButtonWidth,
                      DMButton::height()};
        arrow_button_->set_rect(rect);
        arrow_button_->set_text(expanded_ ? "▼" : "▲");
    }
}

void FullScreenCollapsible::layout_buttons() {
    int x = header_rect_.x + DMSpacing::item_gap();
    if (title_width_ > 0) {
        x += title_width_ + DMSpacing::item_gap();
    }
    if (!buttons_.empty()) {
        x += DMSpacing::item_gap();
    }
    int right_limit = header_rect_.x + header_rect_.w - (kArrowButtonWidth + DMSpacing::item_gap());
    const int button_gap = DMSpacing::small_gap();
    bool out_of_space = false;
    for (auto& btn : buttons_) {
        if (!btn.widget) continue;
        if (out_of_space || x >= right_limit) {
            btn.widget->set_rect(SDL_Rect{0, 0, 0, 0});
            continue;
        }
        SDL_Rect rect{x, header_rect_.y + (kHeaderHeight - DMButton::height()) / 2, 120, DMButton::height()};
        if (rect.x + rect.w > right_limit) {
            btn.widget->set_rect(SDL_Rect{0, 0, 0, 0});
            out_of_space = true;
            continue;
        }
        btn.widget->set_rect(rect);
        x += rect.w + button_gap;
    }
}

void FullScreenCollapsible::set_button_active_state(const std::string& id, bool active) {
    for (auto& btn : buttons_) {
        if (btn.id == id) {
            btn.active = active && !btn.momentary;
            if (btn.momentary && active) {
                btn.active = false;
            }
        }
    }
}

bool FullScreenCollapsible::contains(int x, int y) const {
    if (!visible_) return false;
    SDL_Point p{x, y};
    if (SDL_PointInRect(&p, &header_rect_)) {
        return true;
    }
    if (expanded_ && SDL_PointInRect(&p, &content_rect_)) {
        return true;
    }
    return false;
}

