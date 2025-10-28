#include "FrameEditor.hpp"

#include <algorithm>

#include "../../../dm_styles.hpp"
#include "../../../draw_utils.hpp"
#include "../../../widgets.hpp"
#include "../PreviewProvider.hpp"
#include "movement/FrameMovementEditor.hpp"

namespace animation_editor {
namespace {
constexpr int kTabButtonWidth = 140;
constexpr int kModeControlsPreferredHeight = 240;
constexpr int kModeControlsMinHeight = 160;
constexpr int kFrameDisplayWidth = 640;
constexpr int kFrameDisplayHeight = 360;
constexpr int kFrameListPreferredHeight = 160;
constexpr int kFrameListMinHeight = 96;
constexpr int kNavigationButtonWidth = 64;
constexpr int kNavigationButtonHeight = 64;
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

void FrameEditor::set_preview_provider(std::shared_ptr<PreviewProvider> provider) {
    preview_provider_ = std::move(provider);
    ensure_children();
    if (movement_editor_) {
        movement_editor_->set_preview_provider(preview_provider_);
    }
}

void FrameEditor::update() {
    ensure_children();
    update_button_styles();
    if (movement_editor_) {
        movement_editor_->update();
    }
    update_navigation_styles();
}

void FrameEditor::render(SDL_Renderer* renderer) const {
    if (!renderer) {
        return;
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    if (header_rect_.w > 0 && header_rect_.h > 0) {
        dm_draw::DrawBeveledRect(renderer, header_rect_, DMStyles::CornerRadius(), DMStyles::BevelDepth(), DMStyles::PanelBG(),
                                 DMStyles::HighlightColor(), DMStyles::ShadowColor(), false, DMStyles::HighlightIntensity(),
                                 DMStyles::ShadowIntensity());
    }

    for (const auto& button : mode_buttons_) {
        if (button) {
            button->render(renderer);
        }
    }

    if (movement_editor_) {
        if (active_mode_ == Mode::Movement) {
            movement_editor_->render(renderer);
        } else {
            movement_editor_->render_frame_list(renderer);
        }
    }

    if (prev_frame_button_) prev_frame_button_->render(renderer);
    if (next_frame_button_) next_frame_button_->render(renderer);
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

    if (prev_frame_button_ && prev_frame_button_->handle_event(e)) {
        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT && movement_editor_ &&
            movement_editor_->can_select_previous_frame()) {
            movement_editor_->select_previous_frame();
            update_navigation_styles();
        }
        return true;
    }

    if (next_frame_button_ && next_frame_button_->handle_event(e)) {
        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT && movement_editor_ &&
            movement_editor_->can_select_next_frame()) {
            movement_editor_->select_next_frame();
            update_navigation_styles();
        }
        return true;
    }

    if (active_mode_ == Mode::Movement && movement_editor_ && movement_editor_->handle_event(e)) {
        update_navigation_styles();
        return true;
    }

    if (movement_editor_ && active_mode_ != Mode::Movement && movement_editor_->handle_frame_list_event(e)) {
        update_navigation_styles();
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

    if (!prev_frame_button_) {
        prev_frame_button_ = std::make_unique<DMButton>("<", &default_style, kNavigationButtonWidth, kNavigationButtonHeight);
    }
    if (!next_frame_button_) {
        next_frame_button_ = std::make_unique<DMButton>(">", &default_style, kNavigationButtonWidth, kNavigationButtonHeight);
    }

    if (!movement_editor_) {
        movement_editor_ = std::make_unique<FrameMovementEditor>();
        movement_editor_->set_close_callback([this]() {
            if (close_callback_) {
                close_callback_();
            }
        });
        movement_editor_->set_preview_provider(preview_provider_);
        movement_editor_->set_document(document_);
        movement_editor_->set_animation_id(animation_id_);
        movement_editor_->set_layout_sections(mode_controls_rect_, frame_display_rect_, frame_list_rect_);
    } else {
        movement_editor_->set_preview_provider(preview_provider_);
        movement_editor_->set_document(document_);
        movement_editor_->set_animation_id(animation_id_);
        movement_editor_->set_layout_sections(mode_controls_rect_, frame_display_rect_, frame_list_rect_);
    }
    update_button_styles();
    update_navigation_styles();
}

void FrameEditor::update_layout() {
    const int padding = DMSpacing::panel_padding();
    int gap_header_mode = DMSpacing::small_gap();
    int gap_mode_display = DMSpacing::small_gap();
    int gap_display_list = DMSpacing::small_gap();

    int header_height = DMButton::height() + DMSpacing::small_gap() * 2;
    int mode_controls_height = kModeControlsPreferredHeight;
    int frame_list_height = kFrameListPreferredHeight;
    const int display_height = kFrameDisplayHeight;

    int total_height = padding * 2 + header_height + gap_header_mode + mode_controls_height + gap_mode_display + display_height +
                       gap_display_list + frame_list_height;
    int shortage = total_height - bounds_.h;

    if (shortage > 0) {
        int reduce = std::min(shortage, mode_controls_height - kModeControlsMinHeight);
        mode_controls_height -= reduce;
        shortage -= reduce;
    }
    if (shortage > 0) {
        int reduce = std::min(shortage, frame_list_height - kFrameListMinHeight);
        frame_list_height -= reduce;
        shortage -= reduce;
    }
    if (shortage > 0) {
        int gaps[3] = {gap_header_mode, gap_mode_display, gap_display_list};
        while (shortage > 0) {
            bool reduced = false;
            for (int i = 0; i < 3 && shortage > 0; ++i) {
                if (gaps[i] > 0) {
                    --gaps[i];
                    --shortage;
                    reduced = true;
                }
            }
            if (!reduced) break;
        }
        gap_header_mode = gaps[0];
        gap_mode_display = gaps[1];
        gap_display_list = gaps[2];
    }
    if (shortage > 0) {
        int min_header = DMButton::height();
        int reduce = std::min(shortage, header_height - min_header);
        header_height -= reduce;
        shortage -= reduce;
    }
    if (shortage > 0) {
        int reduce = std::min(shortage, frame_list_height);
        frame_list_height -= reduce;
        shortage -= reduce;
    }

    header_rect_ = SDL_Rect{bounds_.x + padding, bounds_.y + padding, std::max(0, bounds_.w - 2 * padding),
                             std::max(0, header_height)};

    int button_y = header_rect_.y + (header_rect_.h > DMButton::height() ? (header_rect_.h - DMButton::height()) / 2 : 0);
    int button_x = header_rect_.x + DMSpacing::small_gap();
    for (auto& button : mode_buttons_) {
        if (button) {
            button->set_rect(SDL_Rect{button_x, button_y, kTabButtonWidth, DMButton::height()});
            button_x += kTabButtonWidth + DMSpacing::small_gap();
        }
    }

    mode_controls_rect_ = SDL_Rect{header_rect_.x, header_rect_.y + header_rect_.h + gap_header_mode, header_rect_.w,
                                   std::max(0, mode_controls_height)};

    int center_top = mode_controls_rect_.y + mode_controls_rect_.h + gap_mode_display;
    const int available_width = std::max(0, bounds_.w - 2 * padding);
    const int nav_gap = DMSpacing::small_gap();
    int nav_width = kNavigationButtonWidth;
    if (available_width < nav_width * 2 + nav_gap * 2) {
        nav_width = std::max(0, (available_width - nav_gap * 2) / 2);
    }
    int display_width = std::min(kFrameDisplayWidth, std::max(0, available_width - nav_width * 2 - nav_gap * 2));
    int total_center_width = display_width + nav_width * 2 + nav_gap * 2;
    int start_x = bounds_.x + padding + std::max(0, (available_width - total_center_width) / 2);
    int prev_x = start_x;
    int display_x = prev_x + nav_width + nav_gap;
    int next_x = display_x + display_width + nav_gap;

    frame_display_rect_ = SDL_Rect{display_x, center_top, display_width, std::max(0, display_height)};
    int nav_height = std::min(kNavigationButtonHeight, frame_display_rect_.h);
    if (nav_height < 0) nav_height = 0;
    int nav_y = frame_display_rect_.y + (frame_display_rect_.h > nav_height ? (frame_display_rect_.h - nav_height) / 2 : 0);
    prev_button_rect_ = SDL_Rect{prev_x, nav_y, nav_width, nav_height};
    next_button_rect_ = SDL_Rect{next_x, nav_y, nav_width, nav_height};

    frame_list_rect_ = SDL_Rect{header_rect_.x, frame_display_rect_.y + frame_display_rect_.h + gap_display_list, header_rect_.w,
                                std::max(0, frame_list_height)};

    if (prev_frame_button_) prev_frame_button_->set_rect(prev_button_rect_);
    if (next_frame_button_) next_frame_button_->set_rect(next_button_rect_);
    if (movement_editor_) {
        movement_editor_->set_layout_sections(mode_controls_rect_, frame_display_rect_, frame_list_rect_);
    }
    update_navigation_styles();
}

void FrameEditor::set_mode(Mode mode) {
    if (active_mode_ == mode) {
        return;
    }
    active_mode_ = mode;
    update_button_styles();
    update_navigation_styles();
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

void FrameEditor::update_navigation_styles() const {
    const DMButtonStyle& enabled_style = DMStyles::AccentButton();
    const DMButtonStyle& disabled_style = DMStyles::HeaderButton();
    bool movement_ready = movement_editor_ != nullptr;
    if (prev_frame_button_) {
        bool can_step_back = movement_ready && movement_editor_->can_select_previous_frame();
        prev_frame_button_->set_style(can_step_back ? &enabled_style : &disabled_style);
    }
    if (next_frame_button_) {
        bool can_step_forward = movement_ready && movement_editor_->can_select_next_frame();
        next_frame_button_->set_style(can_step_forward ? &enabled_style : &disabled_style);
    }
}

}

