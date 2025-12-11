#include "FrameEditor.hpp"

#include <algorithm>
#include <string>

#include "../../../dm_styles.hpp"
#include "../../../draw_utils.hpp"
#include "../../../widgets.hpp"
#include "../PreviewProvider.hpp"
#include "movement/FrameMovementEditor.hpp"
#include "children/FrameChildrenEditor.hpp"
#include "FrameToolsPanel.hpp"

namespace animation_editor {
namespace {
constexpr int kTabButtonWidth = 140;
constexpr int kModeControlsPreferredHeight = 180;
constexpr int kModeControlsMinHeight = 160;
constexpr int kFrameDisplayWidth = 640;
constexpr int kFrameDisplayHeight = 360;
constexpr int kFrameListPreferredHeight = 160;
constexpr int kFrameListMinHeight = 96;
constexpr int kNavigationButtonWidth = 64;
constexpr int kNavigationButtonHeight = 64;
constexpr int kToolsPanelWidth = 360;

constexpr int kModeControlsYOffset = -4;
constexpr int kFrameListYOffset    =  4;

bool is_children_mode(FrameEditor::Mode mode) {
    return mode == FrameEditor::Mode::StaticChildren ||
           mode == FrameEditor::Mode::AsyncChildren;
}
}

FrameEditor::FrameEditor() { ensure_children(); }

FrameEditor::~FrameEditor() = default;

void FrameEditor::set_document(std::shared_ptr<AnimationDocument> document) {
    document_ = std::move(document);
    ensure_children();
    if (movement_editor_) {
        movement_editor_->set_document(document_);
    }
    if (children_editor_) {
        children_editor_->set_document(document_);
    }
}

void FrameEditor::set_animation_id(const std::string& animation_id) {
    animation_id_ = animation_id;
    ensure_children();
    if (movement_editor_) {
        movement_editor_->set_animation_id(animation_id_);
    }
    if (children_editor_) {
        children_editor_->set_animation_id(animation_id_);
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
    if (children_editor_) {
        children_editor_->set_preview_provider(preview_provider_);
    }
}

void FrameEditor::set_frame_changed_callback(FrameChangedCallback callback) {
    frame_changed_callback_ = std::move(callback);
    ensure_children();
    if (movement_editor_) {
        movement_editor_->set_frame_changed_callback([this](int index) {
            if (frame_changed_callback_) {
                frame_changed_callback_(index);
            }
        });
    }
}

void FrameEditor::set_grid_snap_resolution(int r) {
    ensure_children();
    if (movement_editor_) {
        movement_editor_->set_grid_snap_resolution(r);
    }
}

int FrameEditor::selected_index() const {
    if (!movement_editor_) return 0;
    return movement_editor_->selected_index();
}

void FrameEditor::update() {
    ensure_children();
    update_button_styles();
    if (movement_editor_) {
        movement_editor_->update();
    }
    if (children_editor_ && movement_editor_) {
        children_editor_->set_selected_frame(movement_editor_->selected_index());
        children_editor_->update();
    }
    if (movement_editor_ && children_editor_) {
        int override_count = -1;
        std::string override_animation_id;
        if (active_mode_ == Mode::AsyncChildren && preview_provider_) {
            std::string child_id = children_editor_->selected_child_id();
            AnimationChildMode mode = children_editor_->selected_child_mode();
            if (!child_id.empty() && mode == AnimationChildMode::Async) {
                auto default_anim_id = [](const std::string& child) {
                    const std::string suffix = "/default";
                    if (child.size() >= suffix.size() &&
                        child.rfind(suffix) == child.size() - suffix.size()) {
                        return child;
                    }
                    return child + suffix;
};
                std::string candidate = default_anim_id(child_id);
                int child_frames = preview_provider_->get_frame_count(candidate);
                if (child_frames <= 0) {
                    child_frames = preview_provider_->get_frame_count(child_id);
                    if (child_frames > 0) {
                        candidate = child_id;
                    }
                }
                if (child_frames > 0) {
                    override_count = child_frames;
                    override_animation_id = candidate;
                }
            }
        }
        movement_editor_->set_frame_list_override(override_count, override_animation_id, true);
    }

    if (tools_panel_ && movement_editor_) {
        auto totals = movement_editor_->total_displacement();
        tools_panel_->set_totals(totals.first, totals.second, true );
        tools_panel_->set_show_animation(movement_editor_->show_animation());
    }
    if (tools_panel_) {

        SDL_Rect panel_rect = tools_panel_->rect();
        SDL_Rect work = bounds_;
        const int min_w = 200;
        const int min_h = 160;
        if (panel_rect.w <= 0 || panel_rect.h <= 0) {

            if (tools_panel_rect_.w > 0 && tools_panel_rect_.h > 0) {
                panel_rect = tools_panel_rect_;
            } else {
                panel_rect = SDL_Rect{work.x + DMSpacing::panel_padding(), work.y + DMSpacing::panel_padding(),
                                      std::max(min_w, work.w / 3), std::max(min_h, work.h / 2)};
            }
            tools_panel_->set_rect(panel_rect);
        } else {

            const int margin = DMSpacing::panel_padding();
            int max_x = work.x + work.w - panel_rect.w - margin;
            int max_y = work.y + work.h - panel_rect.h - margin;
            panel_rect.x = std::clamp(panel_rect.x, work.x + margin, std::max(work.x + margin, max_x));
            panel_rect.y = std::clamp(panel_rect.y, work.y + margin, std::max(work.y + margin, max_y));
            tools_panel_->set_rect(panel_rect);
        }
    }
    update_navigation_styles();
}

void FrameEditor::render(SDL_Renderer* renderer) const {
    if (!renderer) {
        return;
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    if (tools_panel_) {
        tools_panel_->set_work_area_bounds(bounds_);
    }
    if (header_rect_.w > 0 && header_rect_.h > 0) {
        dm_draw::DrawBeveledRect(renderer, header_rect_, DMStyles::CornerRadius(), DMStyles::BevelDepth(), DMStyles::PanelBG(), DMStyles::HighlightColor(), DMStyles::ShadowColor(), false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());
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

            movement_editor_->render_canvas_only(renderer);
            if (is_children_mode(active_mode_) && children_editor_) {
                children_editor_->render(renderer);
            }
            movement_editor_->render_frame_list(renderer);
        }
    }

    if (prev_frame_button_) prev_frame_button_->render(renderer);
    if (next_frame_button_) next_frame_button_->render(renderer);
    if (tools_panel_ && tools_panel_->is_visible()) tools_panel_->render(renderer);
}

bool FrameEditor::handle_event(const SDL_Event& e) {
    ensure_children();
    auto compute_pointer_in_tools = [this](const SDL_Event& evt) -> bool {
        if (evt.type != SDL_MOUSEMOTION && evt.type != SDL_MOUSEBUTTONDOWN &&
            evt.type != SDL_MOUSEBUTTONUP && evt.type != SDL_MOUSEWHEEL) {
            return false;
        }
        SDL_Point p;
        if (evt.type == SDL_MOUSEMOTION) {
            p = SDL_Point{evt.motion.x, evt.motion.y};
        } else if (evt.type == SDL_MOUSEWHEEL) {
            int mx = 0, my = 0;
            SDL_GetMouseState(&mx, &my);
            p = SDL_Point{mx, my};
        } else {
            p = SDL_Point{evt.button.x, evt.button.y};
        }
        SDL_Rect tools_bounds = tools_panel_hit_rect();
        return SDL_PointInRect(&p, &tools_bounds) != 0;
};
    bool pointer_in_tools = compute_pointer_in_tools(e);
    for (size_t i = 0; i < mode_buttons_.size(); ++i) {
        auto& button = mode_buttons_[i];
        if (button && button->handle_event(e)) {

            Mode target_mode;
            switch (i) {
                case 0: target_mode = Mode::Movement; break;
                case 1: target_mode = Mode::StaticChildren; break;
                case 2: target_mode = Mode::AttackGeometry; break;
                case 3: target_mode = Mode::HitGeometry; break;
                default: target_mode = Mode::Movement; break;
            }
            set_mode(target_mode);
            return true;
        }
    }

    if (tools_panel_) {
        SDL_Rect before_rect = tools_panel_->rect();
        bool consumed = tools_panel_->handle_event(e);
        SDL_Rect after_rect = tools_panel_->rect();
        const bool pointer_event =
            (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEMOTION);
        if (tools_panel_follow_layout_ && pointer_event &&
            (before_rect.x != after_rect.x || before_rect.y != after_rect.y)) {
            tools_panel_follow_layout_ = false;
        }
        if (consumed) {
            return true;
        }
        pointer_in_tools = compute_pointer_in_tools(e);
    }

    if (prev_frame_button_) {
        prev_frame_button_->handle_event(e);
    }
    if (next_frame_button_) {
        next_frame_button_->handle_event(e);
    }

    if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
        SDL_Point p{e.button.x, e.button.y};
        if (SDL_PointInRect(&p, &prev_button_rect_) && movement_editor_ &&
            movement_editor_->can_select_previous_frame()) {
            movement_editor_->select_previous_frame();
            update_navigation_styles();
            return true;
        }
        if (SDL_PointInRect(&p, &next_button_rect_) && movement_editor_ &&
            movement_editor_->can_select_next_frame()) {
            movement_editor_->select_next_frame();
            update_navigation_styles();
            return true;
        }
    }

    if (e.type == SDL_MOUSEMOTION || e.type == SDL_MOUSEBUTTONDOWN) {
        SDL_Point p;
        if (e.type == SDL_MOUSEMOTION) { p.x = e.motion.x; p.y = e.motion.y; }
        else { p.x = e.button.x; p.y = e.button.y; }
        if (SDL_PointInRect(&p, &prev_button_rect_) || SDL_PointInRect(&p, &next_button_rect_)) {
            return true;
        }
    }

    if (!pointer_in_tools && children_editor_ && is_children_mode(active_mode_)) {
        if (children_editor_->handle_event(e)) {
            return true;
        }
    }

    if (e.type == SDL_KEYDOWN) {
        if (is_children_mode(active_mode_) && children_editor_) {
            if (children_editor_->handle_key_event(e)) {
                return true;
            }
        }
        if (active_mode_ == Mode::Movement && movement_editor_) {
            if (e.key.keysym.sym == SDLK_LEFT) {
                if (movement_editor_->can_select_previous_frame()) {
                    movement_editor_->select_previous_frame();
                    update_navigation_styles();
                    return true;
                }
            } else if (e.key.keysym.sym == SDLK_RIGHT) {
                if (movement_editor_->can_select_next_frame()) {
                    movement_editor_->select_next_frame();
                    update_navigation_styles();
                    return true;
                }
            }
        }
    }

    if (!pointer_in_tools && active_mode_ == Mode::Movement && movement_editor_ && movement_editor_->handle_event(e)) {
        update_navigation_styles();
        return true;
    }

    if (!pointer_in_tools && movement_editor_ && active_mode_ != Mode::Movement && movement_editor_->handle_frame_list_event(e)) {
        update_navigation_styles();
        return true;
    }

    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
        if (close_callback_) {
            close_callback_();
            return true;
        }
    }

    if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEMOTION) {
        SDL_Point p;
        if (e.type == SDL_MOUSEMOTION) { p.x = e.motion.x; p.y = e.motion.y; }
        else { p.x = e.button.x; p.y = e.button.y; }
        SDL_Rect tools_bounds = tools_panel_hit_rect();
        if (SDL_PointInRect(&p, &header_rect_) || SDL_PointInRect(&p, &mode_controls_rect_) ||
            SDL_PointInRect(&p, &frame_display_rect_) || SDL_PointInRect(&p, &frame_list_rect_) ||
            SDL_PointInRect(&p, &tools_bounds) || SDL_PointInRect(&p, &prev_button_rect_) ||
            SDL_PointInRect(&p, &next_button_rect_)) {
            return true;
        }
    }
    return false;
}

void FrameEditor::ensure_children() {
    const DMButtonStyle& default_style = DMStyles::HeaderButton();
    const char* labels[] = {"Movement", "Children", "Attack Geometry", "Hit Geometry"};
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
        movement_editor_->set_frame_changed_callback([this](int index) {
            if (frame_changed_callback_) {
                frame_changed_callback_(index);
            }
        });

        movement_editor_->set_show_animation(true);
    } else {
        movement_editor_->set_preview_provider(preview_provider_);
        movement_editor_->set_document(document_);
        movement_editor_->set_animation_id(animation_id_);
        movement_editor_->set_layout_sections(mode_controls_rect_, frame_display_rect_, frame_list_rect_);
        movement_editor_->set_frame_changed_callback([this](int index) {
            if (frame_changed_callback_) {
                frame_changed_callback_(index);
            }
        });
    }
    if (!children_editor_) {
        children_editor_ = std::make_unique<FrameChildrenEditor>();
    }
    if (children_editor_) {
        children_editor_->set_document(document_);
        children_editor_->set_animation_id(animation_id_);
        children_editor_->set_preview_provider(preview_provider_);
        children_editor_->set_canvas(movement_editor_ ? movement_editor_->canvas() : nullptr);
    }

    if (!tools_panel_) {
        tools_panel_ = std::make_unique<FrameToolsPanel>();
        tools_panel_->set_mode(static_cast<FrameToolsPanel::Mode>(static_cast<int>(active_mode_)));
        tools_panel_->set_callbacks(

            [this](bool smooth) { if (movement_editor_) movement_editor_->set_smoothing_enabled(smooth); },

            [this](bool curve) { if (movement_editor_) movement_editor_->set_curve_enabled(curve); },

            [this](bool show) { if (movement_editor_) movement_editor_->set_show_animation(show); },

            [this](int dx, int dy) { if (movement_editor_) movement_editor_->set_total_displacement(dx, dy); }
        );
        tools_panel_->open();
        tools_panel_follow_layout_ = true;
    } else {
        tools_panel_->set_mode(static_cast<FrameToolsPanel::Mode>(static_cast<int>(active_mode_)));
    }
    if (children_editor_) {
        children_editor_->set_tools_panel(tools_panel_.get());
    }
    if (movement_editor_ && movement_editor_->canvas()) {
        movement_editor_->canvas()->set_anchor_follows_movement(active_mode_ == Mode::Movement || is_children_mode(active_mode_));
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

    mode_controls_rect_ = SDL_Rect{header_rect_.x,
                                   header_rect_.y + header_rect_.h + gap_header_mode + kModeControlsYOffset,
                                   header_rect_.w,
                                   std::max(0, mode_controls_height)};

    int center_top = mode_controls_rect_.y + mode_controls_rect_.h + gap_mode_display;
    const int available_width = std::max(0, bounds_.w - 2 * padding);
    const int nav_gap = DMSpacing::small_gap();
    int nav_width = kNavigationButtonWidth;

    int tools_panel_width = (available_width >= kToolsPanelWidth + nav_width * 2 + nav_gap * 2) ? kToolsPanelWidth : 0;
    int remaining_width = available_width - tools_panel_width;

    if (remaining_width < nav_width * 2 + nav_gap * 2) {
        nav_width = std::max(0, (remaining_width - nav_gap * 2) / 2);
    }
    int display_width = std::min(kFrameDisplayWidth, std::max(0, remaining_width - nav_width * 2 - nav_gap * 2));
    int total_center_width = display_width + nav_width * 2 + nav_gap * 2;
    int start_x = bounds_.x + padding + std::max(0, (remaining_width - total_center_width) / 2);
    int prev_x = start_x;
    int display_x = prev_x + nav_width + nav_gap;
    int next_x = display_x + display_width + nav_gap;

    if (is_children_mode(active_mode_)) {

        tools_panel_rect_ = mode_controls_rect_;
        tools_panel_follow_layout_ = true;
    } else {

        if (tools_panel_width > 0) {
            int tools_x = bounds_.x + padding + remaining_width;
            int tools_height = display_height + gap_display_list + frame_list_height;
            tools_panel_rect_ = SDL_Rect{tools_x, center_top, tools_panel_width, tools_height};
        } else {

            SDL_Rect existing = tools_panel_ ? tools_panel_->rect() : SDL_Rect{0, 0, 0, 0};
            if (existing.w > 0 && existing.h > 0) {
                tools_panel_rect_ = existing;
            } else {
                int fallback_w = kToolsPanelWidth;
                int fallback_h = display_height + gap_display_list + frame_list_height;
                int fallback_x = bounds_.x + std::max(0, bounds_.w - fallback_w - padding);
                int fallback_y = bounds_.y + padding;
                tools_panel_rect_ = SDL_Rect{fallback_x, fallback_y, fallback_w, fallback_h};
                tools_panel_follow_layout_ = true;
            }
        }
    }

    if (tools_panel_) {
        tools_panel_->set_work_area_bounds(bounds_);
        if (tools_panel_follow_layout_ && tools_panel_rect_.w > 0 && tools_panel_rect_.h > 0) {
            tools_panel_->set_rect(tools_panel_rect_);
        } else if (tools_panel_rect_.w > 0 && tools_panel_rect_.h > 0 && tools_panel_->rect().w <= 0) {
            tools_panel_->set_rect(tools_panel_rect_);
        }

        if (is_children_mode(active_mode_) && tools_panel_rect_.w > 0 && tools_panel_rect_.h > 0) {
            tools_panel_->set_rect(tools_panel_rect_);
        }
    }

    frame_display_rect_ = SDL_Rect{display_x, center_top, display_width, std::max(0, display_height)};
    int nav_height = std::min(kNavigationButtonHeight, frame_display_rect_.h);
    if (nav_height < 0) nav_height = 0;
    int nav_y = frame_display_rect_.y + (frame_display_rect_.h > nav_height ? (frame_display_rect_.h - nav_height) / 2 : 0);
    prev_button_rect_ = SDL_Rect{prev_x, nav_y, nav_width, nav_height};
    next_button_rect_ = SDL_Rect{next_x, nav_y, nav_width, nav_height};

    frame_list_rect_ = SDL_Rect{header_rect_.x,
                                frame_display_rect_.y + frame_display_rect_.h + gap_display_list + kFrameListYOffset,
                                std::max(0, remaining_width), std::max(0, frame_list_height)};

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
    Mode previous_mode = active_mode_;
    active_mode_ = mode;
    update_button_styles();
    update_navigation_styles();
    if (tools_panel_) {
        tools_panel_->set_mode(static_cast<FrameToolsPanel::Mode>(static_cast<int>(active_mode_)));
    }
    if (children_editor_ && (is_children_mode(previous_mode) || is_children_mode(active_mode_))) {
        children_editor_->refresh_payload_cache_from_document();
    }
    if (movement_editor_ && movement_editor_->canvas()) {
        movement_editor_->canvas()->set_anchor_follows_movement(active_mode_ == Mode::Movement || is_children_mode(active_mode_));
    }
}

SDL_Rect FrameEditor::tools_panel_hit_rect() const {
    if (tools_panel_ && tools_panel_->is_visible()) {
        return tools_panel_->rect();
    }
    return tools_panel_rect_;
}

void FrameEditor::update_button_styles() const {
    const DMButtonStyle& active_style = DMStyles::AccentButton();
    const DMButtonStyle& inactive_style = DMStyles::HeaderButton();
    for (size_t i = 0; i < mode_buttons_.size(); ++i) {

        bool is_active = false;
        switch (i) {
            case 0: is_active = (active_mode_ == Mode::Movement); break;
            case 1: is_active = is_children_mode(active_mode_); break;
            case 2: is_active = (active_mode_ == Mode::AttackGeometry); break;
            case 3: is_active = (active_mode_ == Mode::HitGeometry); break;
        }
        const DMButtonStyle* style = is_active ? &active_style : &inactive_style;
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
