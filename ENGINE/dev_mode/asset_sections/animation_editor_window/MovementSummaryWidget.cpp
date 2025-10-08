#include "MovementSummaryWidget.hpp"

#include <SDL.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

#include "AnimationDocument.hpp"
#include "PanelLayoutConstants.hpp"
#include "dm_styles.hpp"
#include "dev_mode/widgets.hpp"

namespace animation_editor {

namespace {

const int kButtonHeight = DMButton::height();
const int kButtonWidth = 160;

void render_summary_label(SDL_Renderer* renderer, const std::string& text, int x, int y, SDL_Color color) {
    if (!renderer || text.empty()) {
        return;
    }

    const DMLabelStyle& style = DMStyles::Label();
    TTF_Font* font = style.open_font();
    if (!font) {
        return;
    }

    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (!surface) {
        TTF_CloseFont(font);
        return;
    }

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture) {
        SDL_Rect dst{x, y, surface->w, surface->h};
        SDL_RenderCopy(renderer, texture, nullptr, &dst);
        SDL_DestroyTexture(texture);
    }

    SDL_FreeSurface(surface);
    TTF_CloseFont(font);
}

std::string payload_signature(const std::optional<std::string>& payload) {
    if (!payload.has_value()) {
        return {};
    }
    return *payload;
}

int measure_text_width(const DMLabelStyle& style, const std::string& text) {
    TTF_Font* font = style.open_font();
    if (!font) {
        return 0;
    }
    int width = 0;
    if (TTF_SizeUTF8(font, text.c_str(), &width, nullptr) != 0) {
        width = 0;
    }
    TTF_CloseFont(font);
    return width;
}

}  // namespace

MovementSummaryWidget::MovementSummaryWidget() = default;

void MovementSummaryWidget::set_document(std::shared_ptr<AnimationDocument> document) {
    document_ = std::move(document);
    refresh_totals();
}

void MovementSummaryWidget::set_animation_id(const std::string& animation_id) {
    animation_id_ = animation_id;
    refresh_totals();
}

void MovementSummaryWidget::set_bounds(const SDL_Rect& bounds) {
    bounds_ = bounds;

    const int padding = kPanelPadding;
    const int width = std::max(kButtonWidth, std::min(bounds_.w - padding * 2, kButtonWidth));
    const int x = bounds_.x + bounds_.w - padding - width;
    const int y = bounds_.y + bounds_.h - padding - kButtonHeight;
    button_rect_ = SDL_Rect{x, y, width, kButtonHeight};
}

void MovementSummaryWidget::set_edit_callback(EditCallback callback) { edit_callback_ = std::move(callback); }

void MovementSummaryWidget::update() {
    if (!document_) {
        return;
    }

    auto payload = document_->animation_payload(animation_id_);
    std::string signature = payload_signature(payload);
    if (signature != totals_signature_) {
        totals_signature_ = std::move(signature);
        refresh_totals();
    }
}

void MovementSummaryWidget::render(SDL_Renderer* renderer) const {
    if (!renderer) {
        return;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    const SDL_Color& panel_bg = DMStyles::PanelBG();
    SDL_SetRenderDrawColor(renderer, panel_bg.r, panel_bg.g, panel_bg.b, panel_bg.a);
    SDL_RenderFillRect(renderer, &bounds_);

    const SDL_Color& border = DMStyles::Border();
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &bounds_);

    const int padding = kPanelPadding;
    int text_x = bounds_.x + padding;
    int text_y = bounds_.y + padding;

    const SDL_Color text_color = DMStyles::Label().color;
    render_summary_label(renderer, "Total ΔX: " + std::to_string(static_cast<int>(std::lround(total_dx_))), text_x, text_y,
                         text_color);
    text_y += DMStyles::Label().font_size + DMSpacing::small_gap();
    render_summary_label(renderer, "Total ΔY: " + std::to_string(static_cast<int>(std::lround(total_dy_))), text_x, text_y,
                         text_color);

    const DMButtonStyle& button_style = DMStyles::AccentButton();
    SDL_Color button_color = button_style.bg;
    if (button_pressed_) {
        button_color = button_style.press_bg;
    } else if (button_hovered_) {
        button_color = button_style.hover_bg;
    }
    SDL_SetRenderDrawColor(renderer, button_color.r, button_color.g, button_color.b, button_color.a);
    SDL_RenderFillRect(renderer, &button_rect_);

    SDL_SetRenderDrawColor(renderer, button_style.border.r, button_style.border.g, button_style.border.b, button_style.border.a);
    SDL_RenderDrawRect(renderer, &button_rect_);

    const std::string button_text = "Frame Editor";
    int label_width = measure_text_width(button_style.label, button_text);
    int label_x = button_rect_.x + (button_rect_.w - label_width) / 2;
    label_x = std::max(label_x, button_rect_.x + 8);
    int label_y = button_rect_.y + (button_rect_.h - button_style.label.font_size) / 2;
    render_summary_label(renderer, button_text, label_x, label_y, button_style.text);
}

bool MovementSummaryWidget::handle_event(const SDL_Event& e) {
    switch (e.type) {
        case SDL_MOUSEMOTION: {
            SDL_Point p{e.motion.x, e.motion.y};
            button_hovered_ = SDL_PointInRect(&p, &button_rect_) != 0;
            return button_hovered_;
        }
        case SDL_MOUSEBUTTONDOWN: {
            if (e.button.button != SDL_BUTTON_LEFT) {
                return false;
            }
            SDL_Point p{e.button.x, e.button.y};
            if (SDL_PointInRect(&p, &button_rect_)) {
                button_pressed_ = true;
                return true;
            }
            return false;
        }
        case SDL_MOUSEBUTTONUP: {
            if (e.button.button != SDL_BUTTON_LEFT) {
                return false;
            }
            SDL_Point p{e.button.x, e.button.y};
            bool inside = SDL_PointInRect(&p, &button_rect_) != 0;
            bool was_pressed = button_pressed_;
            button_pressed_ = false;
            if (inside && was_pressed) {
                if (edit_callback_) {
                    edit_callback_(animation_id_);
                }
                return true;
            }
            return inside;
        }
        default:
            break;
    }
    return false;
}

void MovementSummaryWidget::refresh_totals() {
    total_dx_ = 0.0f;
    total_dy_ = 0.0f;

    if (!document_ || animation_id_.empty()) {
        return;
    }

    auto payload_dump = document_->animation_payload(animation_id_);
    if (!payload_dump.has_value()) {
        return;
    }

    nlohmann::json payload = nlohmann::json::parse(*payload_dump, nullptr, false);
    if (!payload.is_object() || !payload.contains("movement")) {
        return;
    }

    const nlohmann::json& movement = payload["movement"];
    if (!movement.is_array()) {
        return;
    }

    for (size_t i = 1; i < movement.size(); ++i) {
        const auto& entry = movement[i];
        if (entry.is_array()) {
            if (entry.size() > 0 && entry[0].is_number()) {
                total_dx_ += entry[0].get<float>();
            }
            if (entry.size() > 1 && entry[1].is_number()) {
                total_dy_ += entry[1].get<float>();
            }
        } else if (entry.is_object()) {
            total_dx_ += entry.value("dx", 0.0f);
            total_dy_ += entry.value("dy", 0.0f);
        }
    }
}

}  // namespace animation_editor

