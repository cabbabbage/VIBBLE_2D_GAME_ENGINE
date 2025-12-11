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
#include "dev_mode/draw_utils.hpp"
#include "dev_mode/font_cache.hpp"
#include "dev_mode/widgets.hpp"
#include "string_utils.hpp"

namespace animation_editor {

struct ResolvedMovement {
    float total_dx = 0.0f;
    float total_dy = 0.0f;
    bool derived = false;
    std::string source_id;
    std::vector<std::string> modifiers;
    std::string signature;
};

namespace {

const int kButtonHeight = DMButton::height();
const int kButtonWidth = 160;

using animation_editor::strings::trim_copy;

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

float read_movement_component(const nlohmann::json& entry, int index) {
    if (entry.is_array()) {
        if (index < static_cast<int>(entry.size()) && entry[index].is_number()) {
            return entry[index].get<float>();
        }
        return 0.0f;
    }
    if (entry.is_object()) {
        const char* keys[] = {"dx", "dy"};
        const char* key = (index == 0) ? keys[0] : keys[1];
        if (entry.contains(key) && entry[key].is_number()) {
            return entry[key].get<float>();
        }
    }
    return 0.0f;
}

ResolvedMovement resolve_movement(const AnimationDocument* document, const std::string& animation_id, int depth = 0) {
    ResolvedMovement result;
    result.signature = std::string{"anim:"} + animation_id;
    if (!document || animation_id.empty() || depth > 16) {
        return result;
    }

    auto payload_dump = document->animation_payload(animation_id);
    std::string payload_signature = payload_dump.has_value() ? *payload_dump : std::string{};
    if (!payload_dump.has_value()) {
        result.signature += "|empty";
        return result;
    }

    nlohmann::json payload = nlohmann::json::parse(*payload_dump, nullptr, false);
    if (payload.is_discarded() || !payload.is_object()) {
        result.signature = payload_signature + "|invalid";
        return result;
    }

    const nlohmann::json* source = payload.contains("source") && payload["source"].is_object() ? &payload["source"] : nullptr;
    std::string kind = source ? source->value("kind", std::string{"folder"}) : std::string{"folder"};

    if (kind == "animation") {

        bool inherit_movement = payload.value("inherit_source_movement", true);
        if (!inherit_movement) {

            kind = "folder";
        }
    }

    if (kind == "animation") {
        bool reverse = payload.value("reverse_source", false);
        bool flip_x = payload.value("flipped_source", false);
        bool flip_y = false;
        bool flip_movement_x = false;
        bool flip_movement_y = false;
        if (payload.contains("derived_modifiers") && payload["derived_modifiers"].is_object()) {
            const auto& modifiers = payload["derived_modifiers"];
            reverse = modifiers.value("reverse", reverse);
            flip_x = modifiers.value("flipX", flip_x);
            flip_y = modifiers.value("flipY", false);
            flip_movement_x = modifiers.value("flipMovementX", flip_movement_x);
            flip_movement_y = modifiers.value("flipMovementY", flip_movement_y);
        }

        std::string reference = source ? source->value("name", std::string{}) : std::string{};
        if (reference.empty() && source) {
            reference = source->value("path", std::string{});
        }
        reference = trim_copy(reference);
        if (reference.empty()) {
            result.signature = payload_signature + "|missing_ref";
            result.derived = true;
            return result;
        }

        ResolvedMovement nested = resolve_movement(document, reference, depth + 1);
        result.total_dx = nested.total_dx;
        result.total_dy = nested.total_dy;
        result.signature = payload_signature + "|child{" + nested.signature + "}";
        result.derived = true;
        result.source_id = reference;

        if (flip_movement_x) result.total_dx = -result.total_dx;
        if (flip_movement_y) result.total_dy = -result.total_dy;
        if (reverse) result.modifiers.push_back("Reverse");
        if (flip_x) result.modifiers.push_back("Flip X");
        if (flip_y) result.modifiers.push_back("Flip Y");
        if (flip_movement_x) result.modifiers.push_back("Flip Movement X");
        if (flip_movement_y) result.modifiers.push_back("Flip Movement Y");

        result.signature += "|mods:";
        result.signature.push_back(reverse ? '1' : '0');
        result.signature.push_back(flip_x ? '1' : '0');
        result.signature.push_back(flip_y ? '1' : '0');
        result.signature.push_back(flip_movement_x ? '1' : '0');
        result.signature.push_back(flip_movement_y ? '1' : '0');
        return result;
    }

    const nlohmann::json& movement = payload.contains("movement") ? payload["movement"] : nlohmann::json::array();
    if (!movement.is_array()) {
        result.signature = payload_signature + "|movement:none";
        return result;
    }

    float dx = 0.0f;
    float dy = 0.0f;
    for (size_t i = 1; i < movement.size(); ++i) {
        const nlohmann::json& entry = movement[i];
        dx += read_movement_component(entry, 0);
        dy += read_movement_component(entry, 1);
    }
    result.total_dx = dx;
    result.total_dy = dy;
    result.signature = payload_signature + "|movement";
    return result;
}

}

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
    if (show_button_) {
        const int width = std::max(kButtonWidth, std::min(bounds_.w - padding * 2, kButtonWidth));
        const int x = bounds_.x + bounds_.w - padding - width;
        const int y = bounds_.y + bounds_.h - padding - kButtonHeight;
        button_rect_ = SDL_Rect{x, y, width, kButtonHeight};
    } else {
        button_rect_ = SDL_Rect{0, 0, 0, 0};
        button_hovered_ = false;
        button_pressed_ = false;
    }
}

void MovementSummaryWidget::set_edit_callback(EditCallback callback) {
    edit_callback_ = std::move(callback);
    refresh_totals();
}

void MovementSummaryWidget::set_go_to_source_callback(GoToSourceCallback callback) {
    go_to_source_callback_ = std::move(callback);
    refresh_totals();
}

int MovementSummaryWidget::preferred_height(int) const {
    const int padding = kPanelPadding;
    const int label_height = DMStyles::Label().font_size + DMSpacing::small_gap();
    int height = padding;
    int text_lines = derived_from_animation_ ? static_cast<int>(inherited_message_lines_.empty() ? 1 : inherited_message_lines_.size()) : 2;
    height += label_height * std::max(1, text_lines);
    if (show_button_) {
        height += DMSpacing::small_gap();
        height += DMButton::height();
    }
    height += padding;
    return height;
}

void MovementSummaryWidget::update() {
    if (!document_) {
        return;
    }

    ResolvedMovement resolved = resolve_movement(document_.get(), animation_id_);
    if (resolved.signature != totals_signature_) {
        totals_signature_ = resolved.signature;
        apply_resolved_totals(resolved);
    }
}

void MovementSummaryWidget::render(SDL_Renderer* renderer) const {
    if (!renderer) {
        return;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    dm_draw::DrawBeveledRect( renderer, bounds_, DMStyles::CornerRadius(), DMStyles::BevelDepth(), DMStyles::PanelBG(), DMStyles::HighlightColor(), DMStyles::ShadowColor(), false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());

    const int padding = kPanelPadding;
    int text_x = bounds_.x + padding;
    int text_y = bounds_.y + padding;

    const SDL_Color text_color = DMStyles::Label().color;
    const int label_stride = DMStyles::Label().font_size + DMSpacing::small_gap();
    if (derived_from_animation_) {
        for (const auto& line : inherited_message_lines_) {
            render_summary_label(renderer, line, text_x, text_y, text_color);
            text_y += label_stride;
        }
    } else {
        render_summary_label(renderer, "Total ΔX: " + std::to_string(static_cast<int>(std::lround(total_dx_))), text_x, text_y, text_color);
        text_y += label_stride;
        render_summary_label(renderer, "Total ΔY: " + std::to_string(static_cast<int>(std::lround(total_dy_))), text_x, text_y, text_color);
        text_y += label_stride;
    }

    if (show_button_) {
        const DMButtonStyle& button_style = DMStyles::AccentButton();
        SDL_Color button_color = button_style.bg;
        if (button_pressed_) {
            button_color = button_style.press_bg;
        } else if (button_hovered_) {
            button_color = button_style.hover_bg;
        }
        const int button_radius = std::min(DMStyles::CornerRadius(), std::min(button_rect_.w, button_rect_.h) / 2);
        const int button_bevel = std::min(DMStyles::BevelDepth(), std::max(0, std::min(button_rect_.w, button_rect_.h) / 2));
        dm_draw::DrawBeveledRect( renderer, button_rect_, button_radius, button_bevel, button_color, button_color, button_color, false, 0.0f, 0.0f);

        dm_draw::DrawRoundedOutline( renderer, button_rect_, button_radius, 1, button_style.border);

        const std::string button_text = button_is_go_to_ ? "Go to Source" : "Frame Editor";
        const SDL_Point label_size = DMFontCache::instance().measure_text(button_style.label, button_text);
        int label_width = label_size.x;
        int label_x = button_rect_.x + (button_rect_.w - label_width) / 2;
        label_x = std::max(label_x, button_rect_.x + 8);
        int label_y = button_rect_.y + (button_rect_.h - button_style.label.font_size) / 2;
        render_summary_label(renderer, button_text, label_x, label_y, button_style.text);
    }
}

bool MovementSummaryWidget::handle_event(const SDL_Event& e) {
    if (!show_button_) {
        button_hovered_ = false;
        button_pressed_ = false;
        return false;
    }
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
                if (button_is_go_to_) {
                    if (go_to_source_callback_ && !inherited_source_id_.empty()) {
                        go_to_source_callback_(inherited_source_id_);
                    }
                } else if (edit_callback_) {
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
    ResolvedMovement resolved = resolve_movement(document_.get(), animation_id_);
    totals_signature_ = resolved.signature;
    apply_resolved_totals(resolved);
}

void MovementSummaryWidget::apply_resolved_totals(const ResolvedMovement& resolved) {
    total_dx_ = resolved.total_dx;
    total_dy_ = resolved.total_dy;
    derived_from_animation_ = resolved.derived;
    inherited_source_id_ = resolved.source_id;
    inherited_message_lines_.clear();

    show_button_ = false;
    button_is_go_to_ = false;

    if (derived_from_animation_) {
        std::string target = inherited_source_id_.empty() ? std::string("the source animation") : "animation '" + inherited_source_id_ + "'";
        inherited_message_lines_.push_back("Movement inherits from " + target + ".");
        if (!resolved.modifiers.empty()) {
            std::string joined;
            for (size_t i = 0; i < resolved.modifiers.size(); ++i) {
                if (i > 0) joined.append(", ");
                joined.append(resolved.modifiers[i]);
            }
            inherited_message_lines_.push_back("Modifiers: " + joined + ".");
        } else {
            inherited_message_lines_.push_back("Modifiers: (none).");
        }
        inherited_message_lines_.push_back("Edit the source animation to change it.");
        inherited_message_lines_.push_back("Totals ΔX: " + std::to_string(static_cast<int>(std::lround(total_dx_))) + ", ΔY: " + std::to_string(static_cast<int>(std::lround(total_dy_))) + ".");
        show_button_ = go_to_source_callback_ && !inherited_source_id_.empty();
        button_is_go_to_ = show_button_;
    } else {
        inherited_source_id_.clear();
        inherited_message_lines_.clear();
        show_button_ = static_cast<bool>(edit_callback_);
        button_is_go_to_ = false;
    }

    set_bounds(bounds_);
}

}

