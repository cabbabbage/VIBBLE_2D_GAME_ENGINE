#include "dev_footer_bar.hpp"

#include "draw_utils.hpp"
#include "utils/input.hpp"

#include <SDL.h>
#include <SDL_ttf.h>

#include <algorithm>

namespace {
constexpr int kDefaultFooterHeight = 40;
constexpr int kFooterHorizontalPadding = 20;
constexpr int kFooterVerticalPadding = 6;
constexpr int kFooterGroupGap = 18;
constexpr int kFooterButtonSpacing = 12;
constexpr int kFooterButtonMinWidth = 110;

const DMButtonStyle* button_style_for(const DevFooterBar::Button& btn) {
    if (btn.active) {
        if (btn.active_style_override) {
            return btn.active_style_override;
        }
        if (btn.style_override) {
            return btn.style_override;
        }
        return &DMStyles::AccentButton();
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

}

DevFooterBar::DevFooterBar(std::string title)
    : title_(std::move(title)),
      height_(kDefaultFooterHeight) {
    depth_effects_checkbox_ = std::make_unique<DMCheckbox>("Depth Effects", false);
    grid_checkbox_ = std::make_unique<DMCheckbox>("Show Grid", grid_overlay_enabled_);
    grid_stepper_ = std::make_unique<DMNumericStepper>("Grid Resolution (r)", 0, 10, grid_resolution_);
}

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

    if (depth_effects_checkbox_ && depth_effects_checkbox_->handle_event(e)) {
        used = true;
        if (on_depth_effects_toggle_) {
            on_depth_effects_toggle_(depth_effects_checkbox_->value());
        }
    }

    if (grid_checkbox_ && grid_checkbox_->handle_event(e)) {
        used = true;
        grid_overlay_enabled_ = grid_checkbox_->value();
        if (on_grid_overlay_toggle_) {
            on_grid_overlay_toggle_(grid_overlay_enabled_);
        }
    }

    if (grid_stepper_ && grid_stepper_->handle_event(e)) {
        used = true;
        grid_resolution_ = grid_stepper_->value();
        if (on_grid_resolution_change_) {
            on_grid_resolution_change_(grid_resolution_, true);
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
    const SDL_Color top = DMStyles::PanelHeader();
    const SDL_Color bottom = dm_draw::DarkenColor(top, 0.25f);
    dm_draw::DrawRoundedGradientRect(renderer, rect_, DMStyles::CornerRadius(), top, bottom);
    dm_draw::DrawRoundedOutline(renderer, rect_, DMStyles::CornerRadius(), 1, DMStyles::Border());

    SDL_Color highlight = DMStyles::HighlightColor();
    highlight.a = static_cast<Uint8>(std::clamp<int>(static_cast<int>(highlight.a * 0.35f), 0, 255));
    SDL_SetRenderDrawColor(renderer, highlight.r, highlight.g, highlight.b, highlight.a);
    SDL_RenderDrawLine(renderer, rect_.x, rect_.y, rect_.x + rect_.w - 1, rect_.y);

    const bool draw_separator = (grid_checkbox_ && grid_stepper_) && (title_bounds_.w > 0 || !buttons_.empty());
    if (draw_separator) {
        SDL_Color separator = DMStyles::Border();
        separator.a = static_cast<Uint8>(std::clamp<int>(static_cast<int>(separator.a * 0.8f), 0, 255));
        SDL_SetRenderDrawColor(renderer, separator.r, separator.g, separator.b, separator.a);
        const int separator_x = std::min(rect_.x + rect_.w - 1, grid_controls_right_ + kFooterGroupGap / 2);
        SDL_RenderDrawLine(renderer, separator_x, rect_.y + kFooterVerticalPadding, separator_x, rect_.y + rect_.h - kFooterVerticalPadding);
    }

    if (depth_effects_checkbox_) {
        depth_effects_checkbox_->render(renderer);
    }

    if (grid_checkbox_) {
        grid_checkbox_->render(renderer);
    }
    if (grid_stepper_) {
        grid_stepper_->render(renderer);
    }

    if (title_bounds_.w > 0 && !title_.empty()) {
        int text_y = title_bounds_.y + (title_bounds_.h - DMStyles::Label().font_size) / 2;
        const int text_x = title_bounds_.x;
        draw_label(renderer, title_, text_x, text_y);
    }

    for (const auto& btn : buttons_) {
        if (!btn.widget) continue;
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

std::optional<SDL_Rect> DevFooterBar::button_rect(const std::string& id) const {
    for (const auto& btn : buttons_) {
        if (btn.id != id) continue;
        if (!btn.widget) continue;
        SDL_Rect rect = btn.widget->rect();
        if (rect.w > 0 && rect.h > 0) {
            return rect;
        }
    }
    return std::nullopt;
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
    grid_controls_right_ = rect_.x + kFooterHorizontalPadding;
    title_bounds_ = SDL_Rect{0, 0, 0, 0};
    layout_grid_controls();
    layout_title_region();
    layout_buttons();
}

void DevFooterBar::layout_title_region() {
    title_bounds_ = SDL_Rect{0, 0, 0, 0};
    if (!show_title_ || title_width_ <= 0) {
        return;
    }

    int x = rect_.x + kFooterHorizontalPadding;
    if (grid_checkbox_ && grid_stepper_) {
        x = std::max(x, grid_controls_right_ + kFooterGroupGap);
    }

    const int max_width = std::max(0, rect_.w - (x - rect_.x) - kFooterHorizontalPadding);
    if (max_width <= 0) {
        return;
    }

    const int clamped_width = std::min(title_width_, max_width);
    title_bounds_ = SDL_Rect{x, rect_.y, clamped_width, rect_.h};
}

void DevFooterBar::layout_buttons() {
    int button_start = rect_.x + kFooterHorizontalPadding;
    if (grid_checkbox_ && grid_stepper_) {
        button_start = std::max(button_start, grid_controls_right_ + kFooterGroupGap);
    }
    if (title_bounds_.w > 0) {
        button_start = std::max(button_start, title_bounds_.x + title_bounds_.w + kFooterGroupGap);
    }

    const int right_limit = rect_.x + rect_.w - kFooterHorizontalPadding;
    const int span = right_limit - button_start;
    const int button_gap = kFooterButtonSpacing;

    if (span <= 0) {
        for (auto& btn : buttons_) {
            if (btn.widget) {
                btn.widget->set_rect(SDL_Rect{0, 0, 0, 0});
            }
        }
        return;
    }

    struct ButtonLayoutInfo {
        DMButton* widget = nullptr;
        int width = 0;
};

    std::vector<ButtonLayoutInfo> visible;
    visible.reserve(buttons_.size());
    int total_width = 0;
    int count = 0;
    bool out_of_space = false;

    for (auto& btn : buttons_) {
        if (!btn.widget) continue;

        if (out_of_space) {
            btn.widget->set_rect(SDL_Rect{0, 0, 0, 0});
            continue;
        }

        int width = std::max(btn.widget->preferred_width(), kFooterButtonMinWidth);

        const int projected_total = total_width + width;
        const int projected_count = count + 1;
        const int projected_block = projected_total + button_gap * std::max(0, projected_count - 1);

        if (projected_block > span) {
            btn.widget->set_rect(SDL_Rect{0, 0, 0, 0});
            out_of_space = true;
            continue;
        }

        visible.push_back({btn.widget.get(), width});
        total_width = projected_total;
        count = projected_count;
    }

    if (visible.empty()) {
        return;
    }

    const int y = rect_.y + (rect_.h - DMButton::height()) / 2;
    const int block_width = total_width + button_gap * std::max(0, count - 1);
    int current_x = std::max(button_start, right_limit - block_width);

    for (size_t i = 0; i < visible.size(); ++i) {
        auto& info = visible[i];
        info.widget->set_rect(SDL_Rect{current_x, y, info.width, DMButton::height()});
        current_x += info.width;
        if (i + 1 < visible.size()) {
            current_x += button_gap;
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

void DevFooterBar::set_grid_overlay_enabled(bool enabled) {
    if (grid_overlay_enabled_ != enabled) {
        grid_overlay_enabled_ = enabled;
        if (grid_checkbox_) {
            grid_checkbox_->set_value(enabled);
        }
        if (on_grid_overlay_toggle_) {
            on_grid_overlay_toggle_(enabled);
        }
    }
}

void DevFooterBar::set_grid_resolution(int resolution) {
    if (grid_resolution_ != resolution) {
        grid_resolution_ = resolution;
        if (grid_stepper_) {
            grid_stepper_->set_value(resolution);
        }
        if (on_grid_resolution_change_) {
            on_grid_resolution_change_(resolution, false);
        }
    }
}

void DevFooterBar::set_grid_controls_callbacks(std::function<void(bool)> on_overlay_toggle,
                                               std::function<void(int, bool)> on_resolution_change) {
    on_grid_overlay_toggle_ = std::move(on_overlay_toggle);
    on_grid_resolution_change_ = std::move(on_resolution_change);
}

void DevFooterBar::layout_grid_controls() {
    grid_controls_right_ = rect_.x + kFooterHorizontalPadding;
    if (!depth_effects_checkbox_ || !grid_checkbox_ || !grid_stepper_) {
        return;
    }

    int x = grid_controls_right_;
    const int checkbox_y = rect_.y + (rect_.h - DMCheckbox::height()) / 2;
    const int stepper_y = rect_.y + (rect_.h - DMNumericStepper::height()) / 2;
    const int gap = DMSpacing::small_gap();

    SDL_Rect depth_rect{x, checkbox_y, depth_effects_checkbox_->preferred_width(), DMCheckbox::height()};
    depth_effects_checkbox_->set_rect(depth_rect);
    x += depth_rect.w + gap;

    SDL_Rect checkbox_rect{x, checkbox_y, grid_checkbox_->preferred_width(), DMCheckbox::height()};
    grid_checkbox_->set_rect(checkbox_rect);
    x += checkbox_rect.w + gap;

    constexpr int kStepperWidth = 180;
    SDL_Rect stepper_rect{x, stepper_y, kStepperWidth, DMNumericStepper::height()};
    grid_stepper_->set_rect(stepper_rect);
    grid_controls_right_ = stepper_rect.x + stepper_rect.w;
}

void DevFooterBar::set_depth_effects_enabled(bool enabled) {
    if (depth_effects_checkbox_) {
        depth_effects_checkbox_->set_value(enabled);
    }
}

void DevFooterBar::set_depth_effects_callbacks(std::function<void(bool)> cb) {
    on_depth_effects_toggle_ = std::move(cb);
}
