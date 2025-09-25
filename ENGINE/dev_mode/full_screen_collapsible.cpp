#include "full_screen_collapsible.hpp"

#include "utils/input.hpp"

#include <SDL_ttf.h>

#include <algorithm>

namespace {
constexpr int kDefaultHeaderHeight = 40;
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
    : title_(std::move(title)),
      header_height_(kDefaultHeaderHeight) {
    arrow_button_ = std::make_unique<DMButton>("▲", &DMStyles::HeaderButton(), kArrowButtonWidth, DMButton::height());
}

void FullScreenCollapsible::set_bounds(int width, int height) {
    screen_w_ = width;
    screen_h_ = height;
    layout();
}

void FullScreenCollapsible::set_header_height(int height) {
    const int clamped = std::max(height, kDefaultHeaderHeight);
    if (clamped == header_height_) {
        return;
    }
    header_height_ = clamped;
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
        int arrow_y = header_rect_.y + DMSpacing::item_gap();
        if (header_rect_.h <= DMButton::height() + DMSpacing::item_gap() * 2) {
            arrow_y = header_rect_.y + (header_rect_.h - DMButton::height()) / 2;
        }
        arrow_button_->set_rect(SDL_Rect{header_rect_.x + header_rect_.w - kArrowButtonWidth - DMSpacing::item_gap(),
                                         arrow_y,
                                         kArrowButtonWidth,
                                         DMButton::height()});
    }
    layout();
    if (on_toggle_) on_toggle_(expanded_);
}

void FullScreenCollapsible::set_header_buttons(std::vector<HeaderButton> buttons) {
    buttons_ = std::move(buttons);
    for (auto& btn : buttons_) {
        const DMButtonStyle* style = btn.style_override ? btn.style_override : &DMStyles::HeaderButton();
        btn.widget = std::make_unique<DMButton>(btn.label, style, 120, DMButton::height());
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

    const bool pointer_event =
        (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEMOTION);
    const bool wheel_event = (e.type == SDL_MOUSEWHEEL);

    SDL_Point pointer{0, 0};
    if (pointer_event) {
        pointer.x = (e.type == SDL_MOUSEMOTION) ? e.motion.x : e.button.x;
        pointer.y = (e.type == SDL_MOUSEMOTION) ? e.motion.y : e.button.y;
    } else if (wheel_event) {
        SDL_GetMouseState(&pointer.x, &pointer.y);
    }

    const bool in_header =
        (pointer_event || wheel_event) && SDL_PointInRect(&pointer, &header_rect_);
    const bool in_content =
        expanded_ && (pointer_event || wheel_event) && SDL_PointInRect(&pointer, &content_rect_);

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

    if (!used && expanded_ && content_event_handler_) {
        const bool route_pointer = in_content;
        const bool route_other = !pointer_event && !wheel_event;
        if ((route_pointer || route_other) && content_event_handler_(e)) {
            used = true;
        }
    }

    if (used) {
        return true;
    }

    if (in_header) {
        return true;
    }

    if (in_content) {
        // When expanded, the content area should capture mouse input to
        // prevent click-through interactions with underlying views.
        // Always swallow events occurring within the content rect.
        return true;
    }

    return false;
}

void FullScreenCollapsible::render(SDL_Renderer* renderer) const {
    if (!visible_ || !renderer) return;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    const SDL_Color header_bg = DMStyles::PanelHeader();
    SDL_SetRenderDrawColor(renderer, header_bg.r, header_bg.g, header_bg.b, 240);
    SDL_RenderFillRect(renderer, &header_rect_);
    const SDL_Color border = DMStyles::Border();
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &header_rect_);

    if (expanded_) {
        SDL_Rect content = content_rect_;
        const SDL_Color content_bg = DMStyles::PanelBG();
        SDL_SetRenderDrawColor(renderer, content_bg.r, content_bg.g, content_bg.b, 220);
        SDL_RenderFillRect(renderer, &content);
        SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
        SDL_RenderDrawRect(renderer, &content);
    }

    int text_x = header_rect_.x + DMSpacing::item_gap();
    int text_y = header_rect_.y + (header_rect_.h - DMStyles::Label().font_size) / 2;
    if (header_rect_.h > DMStyles::Label().font_size + DMSpacing::item_gap() * 2) {
        text_y = header_rect_.y + DMSpacing::item_gap();
    }
    draw_label(renderer, title_, text_x, text_y);

    for (const auto& btn : buttons_) {
        if (!btn.widget) continue;
        if (btn.active) {
            SDL_Rect rect = btn.widget->rect();
            const SDL_Color accent = DMStyles::AccentButton().hover_bg;
            SDL_SetRenderDrawColor(renderer, accent.r, accent.g, accent.b, 96);
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
    header_rect_.h = header_height_;
    header_rect_.x = 0;
    header_rect_.y = expanded_ ? 0 : std::max(0, screen_h_ - header_rect_.h);
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
        int arrow_y = header_rect_.y + DMSpacing::item_gap();
        const int centered_y = header_rect_.y + (header_rect_.h - DMButton::height()) / 2;
        if (header_rect_.h <= DMButton::height() + DMSpacing::item_gap() * 2) {
            arrow_y = centered_y;
        }
        SDL_Rect rect{header_rect_.x + header_rect_.w - kArrowButtonWidth - DMSpacing::item_gap(),
                      arrow_y,
                      kArrowButtonWidth,
                      DMButton::height()};
        arrow_button_->set_rect(rect);
        arrow_button_->set_text(expanded_ ? "▼" : "▲");
    }
}

void FullScreenCollapsible::layout_buttons() {
    int button_start = header_rect_.x + DMSpacing::item_gap();
    if (title_width_ > 0) {
        button_start += title_width_ + DMSpacing::item_gap();
    }
    if (!buttons_.empty()) {
        button_start += DMSpacing::item_gap();
    }

    const int right_limit = header_rect_.x + header_rect_.w - (kArrowButtonWidth + DMSpacing::item_gap());
    const int span = right_limit - button_start;
    const int min_gap = DMSpacing::small_gap();

    if (span <= 0) {
        for (auto& btn : buttons_) {
            if (btn.widget) {
                btn.widget->set_rect(SDL_Rect{0, 0, 0, 0});
            }
        }
        return;
    }

    struct ButtonLayoutInfo {
        DMButton* widget;
        int width;
    };

    std::vector<ButtonLayoutInfo> visible;
    visible.reserve(buttons_.size());
    int total_width = 0;
    bool out_of_space = false;

    for (auto& btn : buttons_) {
        if (!btn.widget) continue;

        if (out_of_space) {
            btn.widget->set_rect(SDL_Rect{0, 0, 0, 0});
            continue;
        }

        int width = btn.widget->rect().w;
        if (width <= 0) {
            width = 120;
        }

        int needed = total_width + width;
        if (!visible.empty()) {
            needed += min_gap * static_cast<int>(visible.size());
        }

        if (needed > span) {
            btn.widget->set_rect(SDL_Rect{0, 0, 0, 0});
            out_of_space = true;
            continue;
        }

        visible.push_back({btn.widget.get(), width});
        total_width += width;
    }

    int y = header_rect_.y + DMSpacing::item_gap();
    if (header_rect_.h <= DMButton::height() + DMSpacing::item_gap() * 2) {
        y = header_rect_.y + (header_rect_.h - DMButton::height()) / 2;
    }

    if (visible.empty()) {
        return;
    }

    if (visible.size() == 1) {
        const int span_remaining = span - total_width;
        int x = button_start + span_remaining / 2;
        x = std::max(x, button_start);
        x = std::min(x, right_limit - visible.front().width);
        visible.front().widget->set_rect(SDL_Rect{x, y, visible.front().width, DMButton::height()});
        return;
    }

    const int gaps = static_cast<int>(visible.size()) - 1;
    int remaining_space = span - total_width;
    int base_gap = gaps > 0 ? remaining_space / gaps : 0;
    int extra = gaps > 0 ? remaining_space % gaps : 0;
    int current_x = button_start;
    for (size_t i = 0; i < visible.size(); ++i) {
        auto& info = visible[i];
        info.widget->set_rect(SDL_Rect{current_x, y, info.width, DMButton::height()});
        current_x += info.width;
        if (i + 1 < visible.size()) {
            int gap = base_gap;
            if (extra > 0) {
                ++gap;
                --extra;
            }
            current_x += gap;
        }
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

const FullScreenCollapsible::HeaderButton* FullScreenCollapsible::find_button(const std::string& id) const {
    auto it = std::find_if(buttons_.begin(), buttons_.end(), [&](const HeaderButton& btn) {
        return btn.id == id;
    });
    if (it == buttons_.end()) {
        return nullptr;
    }
    return &(*it);
}

