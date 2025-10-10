#include "SlidingWindowContainer.hpp"

#include <algorithm>
#include <cmath>

#include <SDL_ttf.h>

#include "dm_styles.hpp"
#include "widgets.hpp"
#include "utils/input.hpp"

namespace {
constexpr int kScrollbarWidth = 10;
constexpr int kScrollbarGap = 6;
constexpr int kScrollbarTrackMargin = 4;

void render_label_text(SDL_Renderer* renderer, const std::string& text, const SDL_Rect& rect) {
    if (!renderer || text.empty()) return;
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
        SDL_Rect dst{rect.x, rect.y, surf->w, surf->h};
        SDL_RenderCopy(renderer, tex, nullptr, &dst);
        SDL_DestroyTexture(tex);
    }
    SDL_FreeSurface(surf);
    TTF_CloseFont(font);
}
}

SlidingWindowContainer::SlidingWindowContainer() = default;

void SlidingWindowContainer::set_layout_function(LayoutFunction fn) { layout_function_ = std::move(fn); }
void SlidingWindowContainer::set_render_function(RenderFunction fn) { render_function_ = std::move(fn); }
void SlidingWindowContainer::set_update_function(UpdateFunction fn) { update_function_ = std::move(fn); }
void SlidingWindowContainer::set_event_function(EventFunction fn) { event_function_ = std::move(fn); }
void SlidingWindowContainer::set_header_text(const std::string& text) { header_text_ = text; }
void SlidingWindowContainer::set_header_text_provider(HeaderTextProvider provider) { header_text_provider_ = std::move(provider); }
void SlidingWindowContainer::set_on_close(std::function<void()> cb) { on_close_ = std::move(cb); }

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
}

void SlidingWindowContainer::clear_panel_bounds_override() {
    panel_override_active_ = false;
    panel_override_ = SDL_Rect{0, 0, 0, 0};
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
}

void SlidingWindowContainer::reset_scroll() {
    scroll_ = 0;
    scroll_dragging_ = false;
    scrollbar_dragging_ = false;
}

void SlidingWindowContainer::pulse_header() { pulse_frames_ = 20; }

void SlidingWindowContainer::prepare_layout(int screen_w, int screen_h) const { layout(screen_w, screen_h); }

bool SlidingWindowContainer::is_point_inside(int x, int y) const {
    if (!visible_) return false;
    SDL_Point p{x, y};
    return SDL_PointInRect(&p, &panel_) == SDL_TRUE;
}

void SlidingWindowContainer::update(const Input& input, int screen_w, int screen_h) {
    prepare_layout(screen_w, screen_h);

    if (!visible_) return;

    int mx = input.getX();
    int my = input.getY();
    const bool pointer_in_scroll =
        (mx >= scroll_region_.x && mx < scroll_region_.x + scroll_region_.w && my >= scroll_region_.y && my < scroll_region_.y + scroll_region_.h);
    const bool pointer_in_panel_area =
        (mx >= panel_.x && mx < panel_.x + panel_.w && my >= panel_.y && my < panel_.y + panel_.h);
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

    prepare_layout(screen_w, screen_h);
}

bool SlidingWindowContainer::handle_event(const SDL_Event& e) {
    if (!visible_) return false;

    if (event_function_) {
        if (event_function_(e)) return true;
    }

    if (close_button_) {
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
        pointer_inside_panel = SDL_PointInRect(&pointer, &panel_);
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
            return true;
        }
        if (scroll_dragging_) {
            int dy = pointer.y - scroll_drag_anchor_y_;
            scroll_ = std::max(0, std::min(max_scroll_, scroll_drag_start_scroll_ - dy));
            return true;
        }
    }

    if (pointer_event && e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        if (max_scroll_ > 0) {
            if (SDL_PointInRect(&pointer, &scroll_thumb_rect_)) {
                scrollbar_dragging_ = true;
                scrollbar_drag_offset_ = pointer.y - scroll_thumb_rect_.y;
                return true;
            }
            if (SDL_PointInRect(&pointer, &scroll_track_rect_)) {
                int thumb_h = scroll_thumb_rect_.h;
                int track_h = scroll_track_rect_.h;
                if (track_h > 0 && thumb_h > 0) {
                    int min_thumb_y = scroll_track_rect_.y;
                    int max_thumb_y = scroll_track_rect_.y + std::max(0, track_h - thumb_h);
                    int desired = pointer.y - thumb_h / 2;
                    desired = std::clamp(desired, min_thumb_y, max_thumb_y);
                    int range = std::max(0, max_thumb_y - min_thumb_y);
                    if (range > 0 && max_scroll_ > 0) {
                        double ratio = static_cast<double>(desired - min_thumb_y) / static_cast<double>(range);
                        scroll_ = std::max(0, std::min(max_scroll_, static_cast<int>(std::round(ratio * max_scroll_))));
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

    if (pointer_inside_panel || scroll_dragging_ || scrollbar_dragging_) {
        return true;
    }

    return false;
}

void SlidingWindowContainer::render(SDL_Renderer* renderer, int screen_w, int screen_h) const {
    if (!visible_) return;

    prepare_layout(screen_w, screen_h);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_Color bg = DMStyles::PanelBG();
    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(renderer, &panel_);

    if (close_button_) {
        close_button_->render(renderer);
    }

    std::string label = header_text_provider_ ? header_text_provider_() : header_text_;
    render_label_text(renderer, label, name_label_rect_);

    if (pulse_frames_ > 0) {
        Uint8 alpha = static_cast<Uint8>(std::clamp(pulse_frames_ * 12, 0, 180));
        SDL_Rect header_rect{panel_.x, panel_.y, panel_.w, DMButton::height()};
        const SDL_Color accent = DMStyles::AccentButton().hover_bg;
        SDL_SetRenderDrawColor(renderer, accent.r, accent.g, accent.b, alpha);
        SDL_RenderFillRect(renderer, &header_rect);
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
    if (content_clip.w > 0 && content_clip.h > 0) {
        SDL_Rect intersection;
        if (SDL_IntersectRect(&panel_clip, &content_clip, &intersection) == SDL_TRUE) {
            SDL_RenderSetClipRect(renderer, &intersection);
        }
    }

    if (render_function_) {
        render_function_(renderer);
    }

    SDL_RenderSetClipRect(renderer, &panel_clip);

    if (max_scroll_ > 0 && scroll_track_rect_.w > 0 && scroll_track_rect_.h > 0) {
        SDL_Color track_col = DMStyles::Border();
        SDL_SetRenderDrawColor(renderer, track_col.r, track_col.g, track_col.b, std::min<int>(track_col.a, 120));
        SDL_RenderFillRect(renderer, &scroll_track_rect_);
        if (scroll_thumb_rect_.h > 0) {
            SDL_Color thumb_col = DMStyles::AccentButton().hover_bg;
            SDL_SetRenderDrawColor(renderer, thumb_col.r, thumb_col.g, thumb_col.b, thumb_col.a);
            SDL_RenderFillRect(renderer, &scroll_thumb_rect_);
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
    scroll_ -= delta;
    if (scroll_ < 0) scroll_ = 0;
    if (scroll_ > max_scroll_) scroll_ = max_scroll_;
}

void SlidingWindowContainer::layout(int screen_w, int screen_h) const {
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
        desired.x = std::max(0, screen_w - desired.w);
        int max_y = screen_h - desired.h;
        if (max_y < 0) max_y = 0;
        desired.y = std::clamp(desired.y, 0, max_y);
        panel_ = desired;
    } else {
        int panel_x = (screen_w * 2) / 3;
        int panel_w = screen_w - panel_x;
        panel_ = SDL_Rect{panel_x, 0, panel_w, screen_h};
    }

    const int padding = DMSpacing::panel_padding();
    const int gap = DMSpacing::section_gap();
    const int content_x = panel_.x + padding;
    const int base_content_w = std::max(0, panel_.w - 2 * padding);
    const int content_top = panel_.y + padding;

    const int label_height = DMButton::height();
    const int label_gap = DMSpacing::item_gap();
    const int close_button_w = label_height;
    const int close_button_gap = DMSpacing::item_gap();
    close_button_rect_ = SDL_Rect{content_x, content_top, close_button_w, label_height};
    int label_x = close_button_rect_.x + close_button_rect_.w + close_button_gap;
    int label_w = std::max(0, (content_x + base_content_w) - label_x);
    name_label_rect_ = SDL_Rect{label_x, content_top, label_w, label_height};

    if (!close_button_) {
        close_button_ = std::make_unique<DMButton>("X", &DMStyles::HeaderButton(), close_button_w, label_height);
    }
    close_button_->set_rect(close_button_rect_);
    int scroll_start = content_top + label_height + label_gap;

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
    int visible_height = panel_.h - padding - label_height - label_gap;
    max_scroll_ = std::max(0, content_height - std::max(0, visible_height));

    if (max_scroll_ > 0) {
        const int scroll_space = kScrollbarWidth + kScrollbarGap;
        int adjusted_content_w = std::max(0, base_content_w - scroll_space);
        if (adjusted_content_w != content_w_active) {
            content_w_active = adjusted_content_w;
            end_y = perform_layout(scroll_, content_w_active);
            content_height = end_y - scroll_start;
            visible_height = panel_.h - padding - label_height - label_gap;
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
        visible_height = panel_.h - padding - label_height - label_gap;
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

    if (max_scroll_ == 0) {
        scroll_dragging_ = false;
        scrollbar_dragging_ = false;
        scroll_track_rect_ = SDL_Rect{0,0,0,0};
        scroll_thumb_rect_ = SDL_Rect{0,0,0,0};
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

