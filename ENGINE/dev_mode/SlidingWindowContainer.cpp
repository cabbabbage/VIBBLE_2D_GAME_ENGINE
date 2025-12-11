#include "SlidingWindowContainer.hpp"

#include <algorithm>
#include <cmath>

#include "dm_icons.hpp"
#include "dm_styles.hpp"
#include "draw_utils.hpp"
#include "font_cache.hpp"
#include "widgets.hpp"
#include "utils/input.hpp"
#include "FloatingPanelLayoutManager.hpp"

namespace {
constexpr int kScrollbarWidth = 10;
constexpr int kScrollbarGap = 6;
constexpr int kScrollbarTrackMargin = 4;
}

SlidingWindowContainer::SlidingWindowContainer() = default;

void SlidingWindowContainer::set_layout_function(LayoutFunction fn) {
    layout_function_ = std::move(fn);
    layout_dirty_ = true;
}
void SlidingWindowContainer::set_render_function(RenderFunction fn) { render_function_ = std::move(fn); }
void SlidingWindowContainer::set_update_function(UpdateFunction fn) { update_function_ = std::move(fn); }
void SlidingWindowContainer::set_event_function(EventFunction fn) { event_function_ = std::move(fn); }
void SlidingWindowContainer::set_header_text(const std::string& text) { header_text_ = text; }
void SlidingWindowContainer::set_header_text_provider(HeaderTextProvider provider) { header_text_provider_ = std::move(provider); }
void SlidingWindowContainer::set_on_close(std::function<void()> cb) { on_close_ = std::move(cb); }

void SlidingWindowContainer::set_header_visible(bool visible) {
    if (header_visible_ == visible) {
        return;
    }
    header_visible_ = visible;
    if (!header_visible_) {
        close_button_.reset();
        pulse_frames_ = 0;
    } else {
        close_button_.reset();
    }
    layout_dirty_ = true;
}

void SlidingWindowContainer::set_close_button_enabled(bool enabled) {
    if (close_button_enabled_ == enabled) {
        return;
    }
    close_button_enabled_ = enabled;
    if (!close_button_enabled_) {
        close_button_.reset();
    }
    layout_dirty_ = true;
}

void SlidingWindowContainer::set_scrollbar_visible(bool visible) {
    if (scrollbar_visible_ == visible) {
        return;
    }
    scrollbar_visible_ = visible;
    if (!scrollbar_visible_) {
        scrollbar_dragging_ = false;
        scroll_dragging_ = false;
        scroll_track_rect_ = SDL_Rect{0, 0, 0, 0};
        scroll_thumb_rect_ = SDL_Rect{0, 0, 0, 0};
    }
    layout(last_screen_w_, last_screen_h_);
}

void SlidingWindowContainer::set_header_navigation_button(const std::string& label,
                                                           std::function<void()> on_click,
                                                           const DMButtonStyle* style) {
    if (label.empty() || !on_click) {
        clear_header_navigation_button();
        return;
    }
    header_nav_callback_ = std::move(on_click);
    const DMButtonStyle* button_style = style ? style : &DMStyles::HeaderButton();
    if (!header_nav_button_) {
        header_nav_button_ = std::make_unique<DMButton>(label, button_style, DMButton::height(), DMButton::height());
    } else {
        header_nav_button_->set_style(button_style);
        header_nav_button_->set_text(label);
    }
    layout_dirty_ = true;
}

void SlidingWindowContainer::clear_header_navigation_button() {
    header_nav_button_.reset();
    header_nav_callback_ = nullptr;
    header_nav_rect_ = SDL_Rect{0, 0, 0, 0};
    layout_dirty_ = true;
}

void SlidingWindowContainer::set_header_navigation_alignment_right(bool align_right) {
    if (header_nav_align_right_ == align_right) {
        return;
    }
    header_nav_align_right_ = align_right;
    layout_dirty_ = true;
}

void SlidingWindowContainer::set_content_clip_enabled(bool enabled) {
    if (content_clip_enabled_ == enabled) {
        return;
    }
    content_clip_enabled_ = enabled;
}

void SlidingWindowContainer::request_layout() {
    layout_dirty_ = true;
}

void SlidingWindowContainer::set_blocks_editor_interactions(bool block) {
    if (blocks_editor_interactions_ == block) {
        return;
    }
    blocks_editor_interactions_ = block;
    update_editor_interaction_block_state();
}

void SlidingWindowContainer::set_editor_interaction_blocker(std::function<void(bool)> blocker) {
    editor_interaction_blocker_ = std::move(blocker);
    bool should_block = blocks_editor_interactions_ && visible_;
    editor_interactions_blocked_ = should_block;
    if (editor_interaction_blocker_) {
        editor_interaction_blocker_(should_block);
    }
}

void SlidingWindowContainer::set_header_visibility_controller(std::function<void(bool)> controller) {
    header_visibility_controller_ = std::move(controller);
    if (header_visibility_controller_) {
        header_visibility_controller_(visible_);
    }
}

void SlidingWindowContainer::set_panel_bounds_override(const SDL_Rect& bounds) {
    panel_override_ = bounds;
    panel_override_active_ = bounds.w > 0 && bounds.h > 0;
    layout_dirty_ = true;
}

void SlidingWindowContainer::clear_panel_bounds_override() {
    panel_override_active_ = false;
    panel_override_ = SDL_Rect{0, 0, 0, 0};
    layout_dirty_ = true;
}

void SlidingWindowContainer::open() { set_visible(true); }
void SlidingWindowContainer::close() {
    if (!visible_) {
        return;
    }
    set_visible(false);
    if (on_close_) {
        on_close_();
    }
}

void SlidingWindowContainer::set_visible(bool visible) {
    if (visible_ == visible) {
        if (!visible_) {
            scroll_dragging_ = false;
            scrollbar_dragging_ = false;
        }
        return;
    }
    visible_ = visible;
    if (!visible_) {
        scroll_dragging_ = false;
        scrollbar_dragging_ = false;
    }
    if (header_visibility_controller_) {
        header_visibility_controller_(visible_);
    }
    update_editor_interaction_block_state();
    layout_dirty_ = true;
}

void SlidingWindowContainer::reset_scroll() {
    layout_dirty_ = true;
    scroll_ = 0;
    scroll_dragging_ = false;
    scrollbar_dragging_ = false;
}

int SlidingWindowContainer::scroll_value() const {
    return scroll_;
}

void SlidingWindowContainer::set_scroll_value(int value) {
    scroll_ = std::max(0, value);
    scroll_dragging_ = false;
    scrollbar_dragging_ = false;
    layout_dirty_ = true;
}

void SlidingWindowContainer::pulse_header() { pulse_frames_ = 20; }

void SlidingWindowContainer::prepare_layout(int screen_w, int screen_h) const {
    if (screen_w != last_screen_w_ || screen_h != last_screen_h_) {
        layout_dirty_ = true;
    }
    if (!layout_dirty_) {
        return;
    }
    layout(screen_w, screen_h);
}

bool SlidingWindowContainer::is_point_inside(int x, int y) const {
    if (!visible_) return false;
    SDL_Point p{x, y};

    if (!header_visible_) {
        const int padding = DMSpacing::panel_padding();
        const int content_top = panel_.y + padding;
        const int label_height = DMButton::height();
        const int label_gap = DMSpacing::item_gap();
        int scroll_start = content_top + (header_visible_ ? (label_height + label_gap) : 0);
        SDL_Rect effective_panel = panel_;
        effective_panel.y = scroll_start;
        effective_panel.h = panel_.h - (scroll_start - panel_.y);
        if (effective_panel.h < 0) effective_panel.h = 0;
        return SDL_PointInRect(&p, &effective_panel) == SDL_TRUE;
    }

    return SDL_PointInRect(&p, &panel_) == SDL_TRUE;
}

void SlidingWindowContainer::update(const Input& input, int screen_w, int screen_h) {
    prepare_layout(screen_w, screen_h);

    if (!visible_) return;

    int mx = input.getX();
    int my = input.getY();
    const bool pointer_in_scroll =
        (mx >= scroll_region_.x && mx < scroll_region_.x + scroll_region_.w && my >= scroll_region_.y && my < scroll_region_.y + scroll_region_.h);
    bool pointer_in_panel_area = false;

    if (!header_visible_) {
        const int padding = DMSpacing::panel_padding();
        const int content_top = panel_.y + padding;
        const int label_height = DMButton::height();
        const int label_gap = DMSpacing::item_gap();
        int scroll_start = content_top + (header_visible_ ? (label_height + label_gap) : 0);
        SDL_Rect effective_panel = panel_;
        effective_panel.y = scroll_start;
        effective_panel.h = panel_.h - (scroll_start - panel_.y);
        if (effective_panel.h < 0) effective_panel.h = 0;
        pointer_in_panel_area =
            (mx >= effective_panel.x && mx < effective_panel.x + effective_panel.w && my >= effective_panel.y && my < effective_panel.y + effective_panel.h);
    } else {
        pointer_in_panel_area =
            (mx >= panel_.x && mx < panel_.x + panel_.w && my >= panel_.y && my < panel_.y + panel_.h);
    }
    if ((pointer_in_scroll || pointer_in_panel_area) && !DMWidgetsSliderScrollCaptured()) {
        int dy = input.getScrollY();
        if (dy != 0) {
            update_scroll_from_delta(dy * 40);
        }
    }

    if (update_function_) {
        update_function_(input, screen_w, screen_h);
    }

    if (pulse_frames_ > 0) {
        --pulse_frames_;
    }
}

bool SlidingWindowContainer::handle_event(const SDL_Event& e) {
    if (last_screen_w_ > 0 && last_screen_h_ > 0) {
        prepare_layout(last_screen_w_, last_screen_h_);
    }

    if (!visible_) return false;

    if (event_function_) {
        if (event_function_(e)) return true;
    }

    if (header_visible_ && header_nav_button_) {
        bool handled = header_nav_button_->handle_event(e);
        if (handled) {
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                if (header_nav_callback_) {
                    header_nav_callback_();
                }
            }
            return true;
        }
    }

    if (header_visible_ && close_button_enabled_ && close_button_) {
        bool handled = close_button_->handle_event(e);
        if (handled) {
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                close();
            }
            return true;
        }
    }

    if (last_screen_w_ <= 0 || last_screen_h_ <= 0) {
        return false;
    }

    bool pointer_event =
        (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEMOTION);
    bool wheel_event = (e.type == SDL_MOUSEWHEEL);
    bool slider_capture_active = DMWidgetsSliderScrollCaptured();

    SDL_Point pointer{0, 0};
    if (pointer_event) {
        pointer.x = (e.type == SDL_MOUSEMOTION) ? e.motion.x : e.button.x;
        pointer.y = (e.type == SDL_MOUSEMOTION) ? e.motion.y : e.button.y;
    }

    if (wheel_event && slider_capture_active) {
        return true;
    }

    bool pointer_inside = false;
    bool pointer_inside_panel = false;
    if (pointer_event) {

        if (!header_visible_) {
            const int padding = DMSpacing::panel_padding();
            const int content_top = panel_.y + padding;
            const int label_height = DMButton::height();
            const int label_gap = DMSpacing::item_gap();
            int scroll_start = content_top + (header_visible_ ? (label_height + label_gap) : 0);
            SDL_Rect effective_panel = panel_;
            effective_panel.y = scroll_start;
            effective_panel.h = panel_.h - (scroll_start - panel_.y);
            if (effective_panel.h < 0) effective_panel.h = 0;
            pointer_inside_panel = SDL_PointInRect(&pointer, &effective_panel);
        } else {
            pointer_inside_panel = SDL_PointInRect(&pointer, &panel_);
        }
        pointer_inside = pointer_inside_panel;
        if (!pointer_inside && !scroll_dragging_ && !scrollbar_dragging_) {
            return false;
        }
    } else if (wheel_event) {
        int mx = 0;
        int my = 0;
        SDL_GetMouseState(&mx, &my);
        SDL_Point p{mx, my};
        pointer_inside = SDL_PointInRect(&p, &scroll_region_);
        pointer_inside_panel = SDL_PointInRect(&p, &panel_);
        if (!pointer_inside && !pointer_inside_panel) {
            return false;
        }
    }

    if (wheel_event) {
        update_scroll_from_delta(e.wheel.y * 40);
        return true;
    }

    if (pointer_event && e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
        bool handled = false;
        if (scroll_dragging_) {
            scroll_dragging_ = false;
            handled = true;
        }
        if (scrollbar_dragging_) {
            scrollbar_dragging_ = false;
            handled = true;
        }
        if (handled) return true;
    }

    if (pointer_event && e.type == SDL_MOUSEMOTION) {
        if (scrollbar_dragging_ && max_scroll_ > 0) {
            int prev_scroll = scroll_;
            int thumb_h = scroll_thumb_rect_.h;
            int track_h = scroll_track_rect_.h;
            if (track_h > 0 && thumb_h > 0) {
                int min_thumb_y = scroll_track_rect_.y;
                int max_thumb_y = scroll_track_rect_.y + std::max(0, track_h - thumb_h);
                int new_thumb_y = pointer.y - scrollbar_drag_offset_;
                new_thumb_y = std::clamp(new_thumb_y, min_thumb_y, max_thumb_y);
                int range = std::max(0, max_thumb_y - min_thumb_y);
                double ratio = (range > 0) ? static_cast<double>(new_thumb_y - min_thumb_y) / static_cast<double>(range) : 0.0;
                scroll_ = std::max(0, std::min(max_scroll_, static_cast<int>(std::round(ratio * max_scroll_))));
            }
            if (scroll_ != prev_scroll) {
                layout_dirty_ = true;
            }
            return true;
        }
        if (scroll_dragging_) {
            int prev_scroll = scroll_;
            int dy = pointer.y - scroll_drag_anchor_y_;
            scroll_ = std::max(0, std::min(max_scroll_, scroll_drag_start_scroll_ - dy));
            if (scroll_ != prev_scroll) {
                layout_dirty_ = true;
            }
            return true;
        }
    }

    if (pointer_event && e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        if (scrollbar_visible_ && max_scroll_ > 0 &&
            scroll_thumb_rect_.w > 0 && scroll_thumb_rect_.h > 0 &&
            scroll_track_rect_.w > 0 && scroll_track_rect_.h > 0) {
            if (SDL_PointInRect(&pointer, &scroll_thumb_rect_)) {
                scrollbar_dragging_ = true;
                scrollbar_drag_offset_ = pointer.y - scroll_thumb_rect_.y;
                return true;
            }
            if (SDL_PointInRect(&pointer, &scroll_track_rect_)) {
                int thumb_h = scroll_thumb_rect_.h;
                int track_h = scroll_track_rect_.h;
                if (track_h > 0 && thumb_h > 0) {
                    int prev_scroll = scroll_;
                    int min_thumb_y = scroll_track_rect_.y;
                    int max_thumb_y = scroll_track_rect_.y + std::max(0, track_h - thumb_h);
                    int desired = pointer.y - thumb_h / 2;
                    desired = std::clamp(desired, min_thumb_y, max_thumb_y);
                    int range = std::max(0, max_thumb_y - min_thumb_y);
                    if (range > 0 && max_scroll_ > 0) {
                        double ratio = static_cast<double>(desired - min_thumb_y) / static_cast<double>(range);
                        scroll_ = std::max(0, std::min(max_scroll_, static_cast<int>(std::round(ratio * max_scroll_))));
                    }
                    if (scroll_ != prev_scroll) {
                        layout_dirty_ = true;
                    }
                }
                scrollbar_dragging_ = true;
                scrollbar_drag_offset_ = scroll_thumb_rect_.h / 2;
                return true;
            }
        }
        if (max_scroll_ > 0 && SDL_PointInRect(&pointer, &scroll_region_)) {
            scroll_dragging_ = true;
            scroll_drag_anchor_y_ = pointer.y;
            scroll_drag_start_scroll_ = scroll_;
            return true;
        }
    }

    bool should_consume_input = false;
    if (scroll_dragging_ || scrollbar_dragging_) {
        should_consume_input = true;
    } else if (pointer_inside_panel) {
        should_consume_input = true;
    }

    if (should_consume_input) {
        return true;
    }

    return false;
}

void SlidingWindowContainer::render(SDL_Renderer* renderer, int screen_w, int screen_h) const {
    if (!visible_) return;

    prepare_layout(screen_w, screen_h);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    const SDL_Color& panel_fill = DMStyles::PanelBG();
    const SDL_Color& panel_highlight = DMStyles::PanelHeader();
    const SDL_Color& panel_shadow = DMStyles::Border();
    dm_draw::DrawBeveledRect( renderer, panel_, DMStyles::CornerRadius(), DMStyles::BevelDepth(), panel_fill, panel_highlight, panel_shadow, true, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());

    SDL_Rect header_region{panel_.x, panel_.y, panel_.w, 0};
    if (header_visible_) {
        header_region.h = std::max(0, scroll_region_.y - panel_.y);
        const int inset = 1;
        if (header_region.h > inset && header_region.w > inset * 2) {
            header_region.x += inset;
            header_region.y += inset;
            header_region.w -= inset * 2;
            header_region.h -= inset;
            dm_draw::DrawBeveledRect( renderer, header_region, 0, DMStyles::BevelDepth(), DMStyles::PanelHeader(), DMStyles::HighlightColor(), DMStyles::ShadowColor(), false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());
        }

        if (pulse_frames_ > 0 && header_region.h > 0 && header_region.w > 0) {
            Uint8 alpha = static_cast<Uint8>(std::clamp(pulse_frames_ * 12, 0, 180));
            const SDL_Color accent = DMStyles::AccentButton().hover_bg;
            SDL_SetRenderDrawColor(renderer, accent.r, accent.g, accent.b, alpha);
            SDL_RenderFillRect(renderer, &header_region);
        }

        if (header_nav_button_) {
            header_nav_button_->render(renderer);
        }
        if (close_button_enabled_ && close_button_) {
            close_button_->render(renderer);
        }
        std::string label = header_text_provider_ ? header_text_provider_() : header_text_;
        DrawLabelText(renderer, label, name_label_rect_, DMStyles::Label());
    }

    SDL_Rect prev_clip;
    SDL_RenderGetClipRect(renderer, &prev_clip);
#if SDL_VERSION_ATLEAST(2,0,4)
    const SDL_bool was_clipping = SDL_RenderIsClipEnabled(renderer);
#else
    const SDL_bool was_clipping = (prev_clip.w != 0 || prev_clip.h != 0) ? SDL_TRUE : SDL_FALSE;
#endif
    SDL_Rect panel_clip = panel_;
    SDL_RenderSetClipRect(renderer, &panel_clip);

    SDL_Rect content_clip = content_clip_rect_;
    if (content_clip_enabled_ && content_clip.w > 0 && content_clip.h > 0) {
        SDL_Rect intersection;
        if (SDL_IntersectRect(&panel_clip, &content_clip, &intersection) == SDL_TRUE) {
            SDL_RenderSetClipRect(renderer, &intersection);
        }
    }

    if (render_function_) {
        render_function_(renderer);
    }

    SDL_RenderSetClipRect(renderer, &panel_clip);

    if (scrollbar_visible_ && max_scroll_ > 0 && scroll_track_rect_.w > 0 && scroll_track_rect_.h > 0) {
        SDL_Rect track = scroll_track_rect_;
        const int track_radius = std::min(DMStyles::CornerRadius(), std::min(track.w, track.h) / 2);
        const int track_bevel = std::min(DMStyles::BevelDepth(), std::max(0, std::min(track.w, track.h) / 2));
        dm_draw::DrawBeveledRect( renderer, track, track_radius, track_bevel, DMStyles::SliderTrackBackground(), DMStyles::HighlightColor(), DMStyles::ShadowColor(), false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());

        if (scroll_thumb_rect_.h > 0) {
            SDL_Rect thumb = scroll_thumb_rect_;
            const int thumb_radius = std::min(DMStyles::CornerRadius(), std::min(thumb.w, thumb.h) / 2);
            const int thumb_bevel = std::min(DMStyles::BevelDepth(), std::max(0, std::min(thumb.w, thumb.h) / 2));
            dm_draw::DrawBeveledRect( renderer, thumb, thumb_radius, thumb_bevel, DMStyles::AccentButton().hover_bg, DMStyles::HighlightColor(), DMStyles::ShadowColor(), true, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());
        }
    }

    if (was_clipping == SDL_TRUE) {
        SDL_RenderSetClipRect(renderer, &prev_clip);
    } else {
        SDL_RenderSetClipRect(renderer, nullptr);
    }
}

void SlidingWindowContainer::update_scroll_from_delta(int delta) {
    if (delta == 0) return;
    int prev_scroll = scroll_;
    scroll_ -= delta;
    if (scroll_ < 0) scroll_ = 0;
    if (scroll_ > max_scroll_) scroll_ = max_scroll_;
    if (scroll_ != prev_scroll) {
        layout_dirty_ = true;
    }
}

void SlidingWindowContainer::layout(int screen_w, int screen_h) const {
    if (!layout_dirty_ && screen_w == last_screen_w_ && screen_h == last_screen_h_) {
        return;
    }

    last_screen_w_ = screen_w;
    last_screen_h_ = screen_h;

    if (screen_w <= 0 || screen_h <= 0) {
        panel_ = SDL_Rect{0, 0, 0, 0};
        scroll_region_ = SDL_Rect{0, 0, 0, 0};
        scroll_track_rect_ = SDL_Rect{0, 0, 0, 0};
        scroll_thumb_rect_ = SDL_Rect{0, 0, 0, 0};
        content_clip_rect_ = SDL_Rect{0, 0, 0, 0};
        close_button_rect_ = SDL_Rect{0, 0, 0, 0};
        if (close_button_) {
            close_button_->set_rect(close_button_rect_);
        }
        max_scroll_ = 0;
        layout_dirty_ = false;
        return;
    }

    if (panel_override_active_) {
        SDL_Rect desired = panel_override_;
        desired.w = std::max(0, desired.w);
        desired.h = std::max(0, desired.h);
        if (desired.w == 0 || desired.h == 0) {
            desired = SDL_Rect{0, 0, screen_w, screen_h};
        }
        if (desired.w > screen_w) desired.w = screen_w;
        if (desired.h > screen_h) desired.h = screen_h;
        int max_x = screen_w - desired.w;
        if (max_x < 0) max_x = 0;
        desired.x = std::clamp(desired.x, 0, max_x);
        int max_y = screen_h - desired.h;
        if (max_y < 0) max_y = 0;
        desired.y = std::clamp(desired.y, 0, max_y);
        panel_ = desired;
    } else {
        const auto usable = FloatingPanelLayoutManager::instance().usableRect();
        int panel_y = usable.y;
        int panel_h = std::max(0, screen_h - usable.y);
        int panel_x = (screen_w * 2) / 3;
        int panel_w = screen_w - panel_x;
        panel_ = SDL_Rect{panel_x, panel_y, panel_w, panel_h};
    }

    const int padding = DMSpacing::panel_padding();
    const int gap = DMSpacing::section_gap();
    const int content_x = panel_.x + padding;
    const int base_content_w = std::max(0, panel_.w - 2 * padding);
    const int content_top = panel_.y + padding;

    const int label_height = header_visible_ ? DMButton::height() : 0;
    const int label_gap = header_visible_ ? DMSpacing::item_gap() : 0;
    const int close_button_w = (header_visible_ && close_button_enabled_) ? label_height : 0;
    const int close_button_gap = (header_visible_ && close_button_enabled_) ? DMSpacing::item_gap() : 0;

    int scroll_start = content_top + (header_visible_ ? (label_height + label_gap) : 0);

    SDL_Rect effective_panel = panel_;
    if (!header_visible_) {
        effective_panel.y = scroll_start;
        effective_panel.h = panel_.h - (scroll_start - panel_.y);
        if (effective_panel.h < 0) effective_panel.h = 0;
    }

    if (header_visible_) {
        int label_start_x = content_x;
        int label_end_x = content_x + base_content_w;

        if (close_button_enabled_) {
            const int close_x = content_x + base_content_w - close_button_w;
            close_button_rect_ = SDL_Rect{close_x, content_top, close_button_w, label_height};
            label_end_x = std::max(content_x, close_x - close_button_gap);
            if (!close_button_) {
                close_button_ = std::make_unique<DMButton>(std::string(DMIcons::Close()), &DMStyles::DeleteButton(), close_button_w, label_height);
            }
            if (close_button_) {
                close_button_->set_rect(close_button_rect_);
                close_button_->set_style(&DMStyles::DeleteButton());
                close_button_->set_text(std::string(DMIcons::Close()));
            }
        } else {
            close_button_rect_ = SDL_Rect{0, 0, 0, 0};
            if (close_button_) {
                close_button_->set_rect(close_button_rect_);
            }
            if (!close_button_enabled_) {
                close_button_.reset();
            }
        }

        if (header_nav_button_) {
            const int nav_gap = DMSpacing::item_gap();
            const int preferred_w = header_nav_button_->preferred_width();
            int nav_width = std::max(DMButton::height(), preferred_w);
            nav_width = std::min(nav_width, std::max(0, label_end_x - content_x));
            if (header_nav_align_right_) {
                int nav_x = std::max(content_x, label_end_x - nav_width);
                header_nav_rect_ = SDL_Rect{nav_x, content_top, nav_width, label_height};
                header_nav_button_->set_rect(header_nav_rect_);
                header_nav_button_->set_style(&DMStyles::HeaderButton());
                header_nav_rect_ = header_nav_button_->rect();
                if (header_nav_rect_.w > 0) {
                    label_end_x = std::max(content_x, header_nav_rect_.x - nav_gap);
                } else {
                    label_end_x = std::max(content_x, header_nav_rect_.x);
                }
            } else {
                header_nav_rect_ = SDL_Rect{content_x, content_top, nav_width, label_height};
                header_nav_button_->set_rect(header_nav_rect_);
                header_nav_button_->set_style(&DMStyles::HeaderButton());
                header_nav_rect_ = header_nav_button_->rect();
                if (header_nav_rect_.w > 0) {
                    label_start_x = std::min(label_end_x, header_nav_rect_.x + header_nav_rect_.w + nav_gap);
                } else {
                    label_start_x = std::min(label_end_x, header_nav_rect_.x);
                }
            }
        } else {
            header_nav_rect_ = SDL_Rect{0, 0, 0, 0};
        }

        int label_w = std::max(0, label_end_x - label_start_x);
        name_label_rect_ = SDL_Rect{label_start_x, content_top, label_w, label_height};

        if (header_nav_button_ && header_nav_rect_.w <= 0) {
            header_nav_button_->set_rect(header_nav_rect_);
        }
    } else {
        close_button_rect_ = SDL_Rect{0, 0, 0, 0};
        name_label_rect_ = SDL_Rect{0, 0, 0, 0};
        header_nav_rect_ = SDL_Rect{0, 0, 0, 0};
        if (close_button_) {
            close_button_->set_rect(close_button_rect_);
        }
        if (!close_button_enabled_) {
            close_button_.reset();
        }
        if (header_nav_button_) {
            header_nav_button_->set_rect(header_nav_rect_);
        }
    }

    int content_w_active = base_content_w;

    auto perform_layout = [&](int scroll_value, int content_width) {
        LayoutContext ctx{content_x, content_width, scroll_value, scroll_start, gap};
        if (layout_function_) {
            return layout_function_(ctx);
        }
        return scroll_start;
};

    int end_y = perform_layout(scroll_, content_w_active);
    int content_height = end_y - scroll_start;
    int visible_height = panel_.h - padding - (header_visible_ ? (label_height + label_gap) : 0);
    max_scroll_ = std::max(0, content_height - std::max(0, visible_height));

    if (scrollbar_visible_ && max_scroll_ > 0) {
        const int scroll_space = kScrollbarWidth + kScrollbarGap;
        int adjusted_content_w = std::max(0, base_content_w - scroll_space);
        if (adjusted_content_w != content_w_active) {
            content_w_active = adjusted_content_w;
            end_y = perform_layout(scroll_, content_w_active);
            content_height = end_y - scroll_start;
            visible_height = panel_.h - padding - (header_visible_ ? (label_height + label_gap) : 0);
            max_scroll_ = std::max(0, content_height - std::max(0, visible_height));
        }
    } else {
        content_w_active = base_content_w;
    }

    int clamped = std::max(0, std::min(max_scroll_, scroll_));
    if (clamped != scroll_) {
        scroll_ = clamped;
        end_y = perform_layout(scroll_, content_w_active);
        content_height = end_y - scroll_start;
        visible_height = panel_.h - padding - (header_visible_ ? (label_height + label_gap) : 0);
        max_scroll_ = std::max(0, content_height - std::max(0, visible_height));
    }

    content_height_px_ = std::max(0, content_height);
    visible_height_px_ = std::max(0, visible_height);

    const int visible_area_h = std::max(0, visible_height);
    const int clip_h = std::max(0, std::min(content_height, visible_area_h));
    const int clip_w = std::max(0, content_w_active);
    const int scroll_top = scroll_start;
    content_clip_rect_ = SDL_Rect{content_x, scroll_top, clip_w, clip_h > 0 ? clip_h : visible_area_h};

    scroll_region_ = SDL_Rect{
        panel_.x,
        scroll_top,
        panel_.w,
        visible_area_h };

    if (!scrollbar_visible_ || max_scroll_ == 0) {
        scroll_dragging_ = false;
        scrollbar_dragging_ = false;
        scroll_track_rect_ = SDL_Rect{0,0,0,0};
        scroll_thumb_rect_ = SDL_Rect{0,0,0,0};
        if (!scrollbar_visible_) {
            return;
        }
    } else {
        const int track_x = panel_.x + panel_.w - padding - kScrollbarWidth;
        const int track_y = scroll_region_.y + kScrollbarTrackMargin;
        const int track_h = std::max(0, scroll_region_.h - 2 * kScrollbarTrackMargin);
        scroll_track_rect_ = SDL_Rect{ track_x, track_y, kScrollbarWidth, track_h };
        if (track_h <= 0) {
            scrollbar_dragging_ = false;
            scroll_thumb_rect_ = SDL_Rect{ track_x, track_y, kScrollbarWidth, 0 };
        } else if (content_height_px_ > 0 && visible_height_px_ > 0) {
            int thumb_h = static_cast<int>(std::round(static_cast<double>(track_h) * visible_height_px_ / std::max(visible_height_px_, content_height_px_)));
            thumb_h = std::clamp(thumb_h, 20, track_h);
            int scroll_range = std::max(0, track_h - thumb_h);
            int thumb_y = track_y;
            if (scroll_range > 0 && max_scroll_ > 0) {
                double ratio = static_cast<double>(scroll_) / static_cast<double>(max_scroll_);
                thumb_y = track_y + static_cast<int>(std::round(ratio * scroll_range));
            }
            thumb_y = std::clamp(thumb_y, track_y, track_y + scroll_range);
            scroll_thumb_rect_ = SDL_Rect{ track_x, thumb_y, kScrollbarWidth, thumb_h };
        } else {
            scrollbar_dragging_ = false;
            scroll_thumb_rect_ = SDL_Rect{ track_x, track_y, kScrollbarWidth, track_h };
        }
    }

    layout_dirty_ = false;
}

void SlidingWindowContainer::update_editor_interaction_block_state() {
    bool should_block = blocks_editor_interactions_ && visible_;
    if (editor_interactions_blocked_ == should_block) {
        return;
    }
    editor_interactions_blocked_ = should_block;
    if (editor_interaction_blocker_) {
        editor_interaction_blocker_(should_block);
    }
}
