#include "dev_footer_bar.hpp"

#include "draw_utils.hpp"
#include "utils/input.hpp"

#include <SDL.h>
#include <SDL_ttf.h>

#include <algorithm>

namespace {
constexpr int kDefaultFooterHeight = 40;

const DMButtonStyle* button_style_for(const DevFooterBar::Button& btn) {
    if (btn.active && btn.active_style_override) {
        return btn.active_style_override;
    }
    if (btn.style_override) {
        return btn.style_override;
    }
    return &DMStyles::HeaderButton();
}

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

}  // namespace

DevFooterBar::DevFooterBar(std::string title)
    : title_(std::move(title)),
      height_(kDefaultFooterHeight) {}

void DevFooterBar::set_bounds(int width, int height) {
    screen_w_ = width;
    screen_h_ = height;
    layout();
}

void DevFooterBar::set_height(int height) {
    const int clamped = std::max(height, kDefaultFooterHeight);
    if (clamped == height_) {
        return;
    }
    height_ = clamped;
    layout();
}

void DevFooterBar::set_title(const std::string& title) {
    if (title_ == title) return;
    title_ = title;
    layout();
}

void DevFooterBar::set_title_visible(bool visible) {
    if (show_title_ == visible) return;
    show_title_ = visible;
    layout();
}

void DevFooterBar::set_buttons(std::vector<Button> buttons) {
    buttons_ = std::move(buttons);
    for (auto& btn : buttons_) {
        const DMButtonStyle* style = button_style_for(btn);
        btn.widget = std::make_unique<DMButton>(btn.label, style, 120, DMButton::height());
    }
    layout_buttons();
}

void DevFooterBar::activate_button(const std::string& id) {
    for (auto& btn : buttons_) {
        const bool new_state = (btn.id == id);
        if (btn.active != new_state) {
            btn.active = new_state;
            if (btn.widget) {
                btn.widget->set_style(button_style_for(btn));
            }
            if (btn.on_toggle) {
                btn.on_toggle(btn.active);
            }
        }
    }
}

void DevFooterBar::set_active_button(const std::string& id, bool trigger_callback) {
    for (auto& btn : buttons_) {
        const bool should_activate = (btn.id == id);
        if (btn.momentary) {
            continue;
        }
        if (btn.active != should_activate) {
            btn.active = should_activate;
            if (btn.widget) {
                btn.widget->set_style(button_style_for(btn));
            }
            if (trigger_callback && btn.on_toggle) {
                btn.on_toggle(btn.active);
            }
        } else if (should_activate && trigger_callback && btn.on_toggle) {
            btn.on_toggle(btn.active);
        }
    }
    if (!trigger_callback) {
        return;
    }
    for (auto& btn : buttons_) {
        if (btn.momentary && btn.id == id && btn.on_toggle) {
            btn.on_toggle(true);
            btn.active = false;
            if (btn.widget) {
                btn.widget->set_style(button_style_for(btn));
            }
        } else if (!btn.momentary && btn.id != id && btn.active) {
            btn.active = false;
            if (btn.widget) {
                btn.widget->set_style(button_style_for(btn));
            }
            if (btn.on_toggle) {
                btn.on_toggle(false);
            }
        }
    }
}

void DevFooterBar::set_button_active_state(const std::string& id, bool active) {
    for (auto& btn : buttons_) {
        if (btn.id == id) {
            bool new_state = active;
            if (btn.momentary && active) {
                new_state = false;
            }
            if (btn.active != new_state) {
                btn.active = new_state;
                if (btn.widget) {
                    btn.widget->set_style(button_style_for(btn));
                }
            }
        }
    }
}

void DevFooterBar::update(const Input&) {}

bool DevFooterBar::handle_event(const SDL_Event& e) {
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

    const bool in_footer = (pointer_event || wheel_event) && SDL_PointInRect(&pointer, &rect_);

    bool used = false;
    for (auto& btn : buttons_) {
        if (!btn.widget) continue;
        if (btn.widget->handle_event(e)) {
            used = true;
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                if (btn.momentary) {
                    if (btn.on_toggle) btn.on_toggle(true);
                    btn.active = false;
                    if (btn.widget) {
                        btn.widget->set_style(button_style_for(btn));
                    }
                } else {
                    if (btn.active) {
                        btn.active = false;
                        if (btn.on_toggle) btn.on_toggle(false);
                        btn.widget->set_style(button_style_for(btn));
                    } else {
                        set_active_button(btn.id, true);
                    }
                }
            }
        }
    }

    if (used) {
        return true;
    }

    if (in_footer) {
        return true;
    }

    return false;
}

void DevFooterBar::render(SDL_Renderer* renderer) const {
    if (!visible_ || !renderer) return;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    dm_draw::DrawBeveledRect(
        renderer,
        rect_,
        DMStyles::CornerRadius(),
        DMStyles::BevelDepth(),
        DMStyles::PanelHeader(),
        DMStyles::HighlightColor(),
        DMStyles::ShadowColor(),
        false,
        DMStyles::HighlightIntensity(),
        DMStyles::ShadowIntensity());

    if (show_title_ && !title_.empty()) {
        int text_x = rect_.x + DMSpacing::item_gap();
        int text_y = rect_.y + (rect_.h - DMStyles::Label().font_size) / 2;
        if (rect_.h > DMStyles::Label().font_size + DMSpacing::item_gap() * 2) {
            text_y = rect_.y + DMSpacing::item_gap();
        }
        draw_label(renderer, title_, text_x, text_y);
    }

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
}

const DevFooterBar::Button* DevFooterBar::find_button(const std::string& id) const {
    for (const auto& btn : buttons_) {
        if (btn.id == id) {
            return &btn;
        }
    }
    return nullptr;
}

bool DevFooterBar::contains(int x, int y) const {
    if (!visible_) return false;
    SDL_Point p{x, y};
    return SDL_PointInRect(&p, &rect_);
}

void DevFooterBar::layout() {
    rect_.w = screen_w_;
    rect_.h = height_;
    rect_.x = 0;
    rect_.y = std::max(0, screen_h_ - rect_.h);
    update_title_width();
    layout_buttons();
}

void DevFooterBar::layout_buttons() {
    int button_start = rect_.x + DMSpacing::item_gap();
    if (title_width_ > 0) {
        button_start += title_width_ + DMSpacing::item_gap();
    }
    if (!buttons_.empty()) {
        button_start += DMSpacing::item_gap();
    }

    const int right_limit = rect_.x + rect_.w - DMSpacing::item_gap();
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

    int y = rect_.y + DMSpacing::item_gap();
    if (rect_.h <= DMButton::height() + DMSpacing::item_gap() * 2) {
        y = rect_.y + (rect_.h - DMButton::height()) / 2;
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

void DevFooterBar::update_title_width() {
    title_width_ = 0;
    if (!show_title_ || title_.empty()) {
        return;
    }
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

