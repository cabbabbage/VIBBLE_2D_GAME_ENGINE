#include "FrameEditor.hpp"

#include <algorithm>

#include "../../../dm_styles.hpp"
#include "../../../widgets.hpp"
#include "movement/FrameMovementEditor.hpp"

namespace animation_editor {
namespace {
constexpr int kTabButtonWidth = 140;
}

FrameEditor::FrameEditor() { ensure_children(); }

FrameEditor::~FrameEditor() = default;

void FrameEditor::set_document(std::shared_ptr<AnimationDocument> document) {
    document_ = std::move(document);
    ensure_children();
    if (movement_editor_) {
        movement_editor_->set_document(document_);
    }
}

void FrameEditor::set_animation_id(const std::string& animation_id) {
    animation_id_ = animation_id;
    ensure_children();
    if (movement_editor_) {
        movement_editor_->set_animation_id(animation_id_);
    }
}

void FrameEditor::set_bounds(const SDL_Rect& bounds) {
    bounds_ = bounds;
    ensure_children();
    update_layout();
}

void FrameEditor::set_close_callback(CloseCallback callback) {
    close_callback_ = std::move(callback);
    ensure_children();
    if (movement_editor_) {
        movement_editor_->set_close_callback([this]() {
            if (close_callback_) {
                close_callback_();
            }
        });
    }
}

void FrameEditor::update() {
    ensure_children();
    update_button_styles();
    if (active_mode_ == Mode::Movement && movement_editor_) {
        movement_editor_->update();
    }
}

void FrameEditor::render(SDL_Renderer* renderer) const {
    if (!renderer) {
        return;
    }
    const SDL_Color& border = DMStyles::Border();
    if (tabs_rect_.w > 0 && tabs_rect_.h > 0) {
        SDL_Rect header_rect = tabs_rect_;
        const SDL_Color& bg = DMStyles::PanelBG();
        SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
        SDL_RenderFillRect(renderer, &header_rect);
        SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
        SDL_RenderDrawRect(renderer, &header_rect);
    }

    for (const auto& button : mode_buttons_) {
        if (button) {
            button->render(renderer);
        }
    }

    if (active_mode_ == Mode::Movement && movement_editor_) {
        movement_editor_->render(renderer);
    }
}

bool FrameEditor::handle_event(const SDL_Event& e) {
    ensure_children();
    for (size_t i = 0; i < mode_buttons_.size(); ++i) {
        auto& button = mode_buttons_[i];
        if (button && button->handle_event(e)) {
            set_mode(static_cast<Mode>(i));
            return true;
        }
    }

    if (active_mode_ == Mode::Movement && movement_editor_ && movement_editor_->handle_event(e)) {
        return true;
    }

    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
        if (close_callback_) {
            close_callback_();
            return true;
        }
    }
    return false;
}

void FrameEditor::ensure_children() {
    const DMButtonStyle& default_style = DMStyles::HeaderButton();
    const char* labels[] = {"Movement", "Children", "Attacking"};
    for (size_t i = 0; i < mode_buttons_.size(); ++i) {
        if (!mode_buttons_[i]) {
            mode_buttons_[i] = std::make_unique<DMButton>(labels[i], &default_style, kTabButtonWidth, DMButton::height());
        }
    }

    if (!movement_editor_) {
        movement_editor_ = std::make_unique<FrameMovementEditor>();
        movement_editor_->set_close_callback([this]() {
            if (close_callback_) {
                close_callback_();
            }
        });
        movement_editor_->set_document(document_);
        movement_editor_->set_animation_id(animation_id_);
        movement_editor_->set_bounds(content_rect());
    }
    update_button_styles();
}

void FrameEditor::update_layout() {
    const int padding = DMSpacing::panel_padding();
    const int gap = DMSpacing::small_gap();
    int x = bounds_.x + padding;
    int y = bounds_.y + padding;
    int max_bottom = y;

    for (auto& button : mode_buttons_) {
        if (button) {
            SDL_Rect rect{x, y, kTabButtonWidth, DMButton::height()};
            button->set_rect(rect);
            max_bottom = std::max(max_bottom, rect.y + rect.h);
            x += kTabButtonWidth + gap;
        }
    }

    tabs_rect_ = SDL_Rect{bounds_.x, bounds_.y, bounds_.w, std::max(0, max_bottom - bounds_.y + padding)};

    if (movement_editor_) {
        movement_editor_->set_bounds(content_rect());
    }
}

void FrameEditor::set_mode(Mode mode) {
    if (active_mode_ == mode) {
        return;
    }
    active_mode_ = mode;
    update_button_styles();
    if (active_mode_ == Mode::Movement && movement_editor_) {
        movement_editor_->set_bounds(content_rect());
    }
}

void FrameEditor::update_button_styles() const {
    const DMButtonStyle& active_style = DMStyles::AccentButton();
    const DMButtonStyle& inactive_style = DMStyles::HeaderButton();
    for (size_t i = 0; i < mode_buttons_.size(); ++i) {
        const DMButtonStyle* style = (static_cast<Mode>(i) == active_mode_) ? &active_style : &inactive_style;
        if (mode_buttons_[i]) {
            mode_buttons_[i]->set_style(style);
        }
    }
}

SDL_Rect FrameEditor::content_rect() const {
    int top = tabs_rect_.y + tabs_rect_.h;
    int height = bounds_.h - (top - bounds_.y);
    if (height < 0) {
        height = 0;
    }
    return SDL_Rect{bounds_.x, top, bounds_.w, height};
}

}

