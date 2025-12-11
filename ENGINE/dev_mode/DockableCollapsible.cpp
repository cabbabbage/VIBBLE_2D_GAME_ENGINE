#include "DockableCollapsible.hpp"

#include "FloatingDockableManager.hpp"
#include "FloatingPanelLayoutManager.hpp"
#include "draw_utils.hpp"
#include "dev_ui_settings.hpp"
#include "dm_icons.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <vector>
#include <SDL_log.h>
#include <SDL_timer.h>

#include "utils/input.hpp"

#ifndef DM_FORCE_LAYOUT
#define DM_FORCE_LAYOUT 0
#endif

namespace {

    constexpr float kPi = 3.14159265358979323846f;

    constexpr int kHeaderDragStartThreshold = 2;
    constexpr Uint32 kPointerBlockOnShowMs = 16;
    constexpr Uint32 kPointerBlockAfterDragMs = 60;

    void draw_lock_icon(SDL_Renderer* r, const SDL_Rect& rect, bool locked) {
        if (!r || rect.w <= 0 || rect.h <= 0) {
            return;
        }

        const SDL_Color stroke = DMStyles::Border();
        const SDL_Color body_fill = locked ? DMStyles::ButtonBaseFill() : dm_draw::LightenColor(DMStyles::ButtonBaseFill(), 0.08f);

        const int horizontal_padding = std::max(1, rect.w / 8);
        SDL_Rect body = rect;
        body.y += rect.h / 2;
        body.h = rect.h - (body.y - rect.y) - 2;
        body.x += horizontal_padding;
        body.w -= horizontal_padding * 2;
        if (body.w < 4) {
            body.w = std::max(4, rect.w - 4);
            body.x = rect.x + (rect.w - body.w) / 2;
        }
        if (body.h < 4) {
            body.h = std::max(4, rect.h / 2);
            body.y = rect.y + rect.h - body.h;
        }

        const int leg_inset = std::max(2, body.w / 6);
        int shackle_left = body.x + leg_inset;
        int shackle_right = body.x + body.w - leg_inset;
        if (shackle_right - shackle_left < 4) {
            const int inset = std::max(1, body.w / 4);
            shackle_left = body.x + inset;
            shackle_right = body.x + body.w - inset;
        }
        if (shackle_right <= shackle_left) {
            const int mid = body.x + body.w / 2;
            shackle_left = mid - 2;
            shackle_right = mid + 2;
        }

        const int shackle_bottom = body.y;
        int shackle_top = rect.y + std::max(1, rect.h / 8);
        if (shackle_top >= shackle_bottom - 2) {
            shackle_top = std::max(rect.y, shackle_bottom - std::max(4, rect.h / 3));
        }
        int arc_height = shackle_bottom - shackle_top;
        if (arc_height < 4) {
            arc_height = std::max(4, rect.h / 3);
            shackle_top = shackle_bottom - arc_height;
        }

        const float cx = static_cast<float>(shackle_left + shackle_right) * 0.5f;
        const float rx = static_cast<float>(shackle_right - shackle_left) * 0.5f;
        const float cy = static_cast<float>(shackle_bottom);
        const float ry = static_cast<float>(arc_height);

        auto draw_thick_segment = [r](int x0, int y0, int x1, int y1) {
            SDL_RenderDrawLine(r, x0, y0, x1, y1);
            SDL_RenderDrawLine(r, x0, y0 + 1, x1, y1 + 1);
};

        SDL_SetRenderDrawColor(r, stroke.r, stroke.g, stroke.b, stroke.a);

        const int arc_steps = 24;
        int prev_x = shackle_right;
        int prev_y = shackle_bottom;
        for (int i = 1; i <= arc_steps; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(arc_steps);
            const float theta = kPi * t;
            const int x = static_cast<int>(std::lround(cx + rx * std::cos(theta)));
            const int y = static_cast<int>(std::lround(cy - ry * std::sin(theta)));
            draw_thick_segment(prev_x, prev_y, x, y);
            prev_x = x;
            prev_y = y;
        }

        const int leg_length = std::max(3, std::min(body.h - 2, rect.h / 3));
        draw_thick_segment(shackle_left, shackle_bottom, shackle_left, shackle_bottom + leg_length);
        if (locked) {
            draw_thick_segment(shackle_right, shackle_bottom, shackle_right, shackle_bottom + leg_length);
        } else {
            const int open_dx = std::max(3, (shackle_right - shackle_left) / 3);
            draw_thick_segment(shackle_right, shackle_bottom, shackle_right + open_dx, shackle_bottom - leg_length / 2);
        }

        const int body_radius = std::min(DMStyles::CornerRadius(), std::min(body.w, body.h) / 3);
        dm_draw::DrawBeveledRect( r, body, body_radius, DMStyles::BevelDepth(), body_fill, DMStyles::HighlightColor(), DMStyles::ShadowColor(), true, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());

        const SDL_Color key_color = dm_draw::DarkenColor(body_fill, 0.45f);
        SDL_SetRenderDrawColor(r, key_color.r, key_color.g, key_color.b, key_color.a);
        const int key_radius = std::max(1, std::min(body.w, body.h) / 6);
        const int key_center_x = body.x + body.w / 2;
        const int key_center_y = body.y + body.h / 2 - key_radius / 2;
        for (int dy = -key_radius; dy <= key_radius; ++dy) {
            const int span = static_cast<int>(std::sqrt(static_cast<double>(key_radius * key_radius - dy * dy)) + 0.5);
            SDL_RenderDrawLine(r, key_center_x - span, key_center_y + dy, key_center_x + span, key_center_y + dy);
        }

        SDL_Rect stem{ key_center_x - std::max(1, key_radius / 3),
                       key_center_y,
                       std::max(2, key_radius / 2), std::max(2, body.h / 3) };
        SDL_RenderFillRect(r, &stem);
    }

    class LayoutTimingScope {
    public:
        LayoutTimingScope(const std::string& title,
                          bool layout_dirty,
                          bool geometry_dirty,
                          bool resized,
                          bool forced)
            : title_(title),
              layout_dirty_(layout_dirty),
              geometry_dirty_(geometry_dirty),
              resized_(resized),
              forced_(forced),
              start_(std::chrono::steady_clock::now()) {
            SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "[DockableCollapsible] layout begin: %s (layout=%s geometry=%s resized=%s forced=%s)", title_.c_str(), layout_dirty_ ? "true" : "false", geometry_dirty_ ? "true" : "false", resized_ ? "true" : "false", forced_ ? "true" : "false");
        }

        ~LayoutTimingScope() {
            auto elapsed = std::chrono::steady_clock::now() - start_;
            double ms = std::chrono::duration<double, std::milli>(elapsed).count();
            SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "[DockableCollapsible] layout end: %s (%.3f ms)", title_.c_str(), ms);
        }

    private:
        std::string title_;
        bool layout_dirty_ = false;
        bool geometry_dirty_ = false;
        bool resized_ = false;
        bool forced_ = false;
        std::chrono::steady_clock::time_point start_;
};
}

DockableCollapsible::DockableCollapsible(const std::string& title, bool floatable,
                                         int x, int y)
    : title_(title) {
    floatable_ = floatable;
    close_button_enabled_ = floatable;
    show_header_ = true;
    scroll_enabled_ = floatable;
    available_height_override_ = -1;
    rect_.x = x; rect_.y = y;
    header_btn_ = std::make_unique<DMButton>(title_, header_button_style_, floating_content_width_, DMButton::height());
    close_btn_  = std::make_unique<DMButton>(std::string(DMIcons::Close()), &DMStyles::DeleteButton(), DMButton::height(), DMButton::height());
    padding_ = DMSpacing::panel_padding();
    row_gap_ = DMSpacing::item_gap();
    col_gap_ = DMSpacing::item_gap();
    if (floatable_) {
        rect_.w = 2 * padding_ + floating_content_width_;
    }
    update_header_button();
    update_layout_manager_registration();
}

DockableCollapsible::~DockableCollapsible() {
    if (registered_with_layout_manager_) {
        FloatingPanelLayoutManager::instance().unregisterPanel(this);
        registered_with_layout_manager_ = false;
    }
}

void DockableCollapsible::set_visible(bool v) {
    if (visible_ == v) {
        return;
    }
    const bool was_visible = visible_;
    visible_ = v;
    if (visible_) {
        block_pointer_for(kPointerBlockOnShowMs);
        if (!was_visible && scroll_enabled_) {
            scroll_ = 0;
            max_scroll_ = 0;
        }
    } else {
        block_pointer_for(0);
    }
    if (!visible_) {
        dragging_ = false;
        drag_exceeded_threshold_ = false;
        header_dragging_via_button_ = false;
        FloatingDockableManager::instance().notify_panel_closed(this);
        if (on_close_) on_close_();
    }
    invalidate_layout();
    update_layout_manager_registration();
}

void DockableCollapsible::open() {
    set_visible(true);
    set_expanded(true);
}

void DockableCollapsible::close() {
    set_visible(false);
}

void DockableCollapsible::set_rows(const Rows& rows) {
    if (locked_) {
        log_locked_mutation("set_rows");
        return;
    }
    rows_ = rows;
    for (auto& row : rows_) {
        for (auto* w : row) {
            if (!w) {
                continue;
            }
            w->set_layout_dirty_callback([this]() {
                this->invalidate_layout();
            });
            w->clear_layout_dirty_flags();
        }
    }
    invalidate_layout();
}

void DockableCollapsible::set_title(const std::string& title) {
    title_ = title;
    update_header_button();
}

void DockableCollapsible::set_expanded(bool e) {
    expanded_ = e;
    update_header_button();
    invalidate_layout();
}

void DockableCollapsible::set_show_header(bool show) {
    if (show_header_ == show) return;
    show_header_ = show;
    if (!show_header_) {
        expanded_ = true;
        header_btn_.reset();
        close_btn_.reset();
    } else {
        int header_w = floatable_ ? floating_content_width_ : 260;
        header_btn_ = std::make_unique<DMButton>(title_, header_button_style_, header_w, DMButton::height());
        if (floatable_ || close_button_enabled_) {
            close_btn_ = std::make_unique<DMButton>(std::string(DMIcons::Close()), &DMStyles::DeleteButton(), DMButton::height(), DMButton::height());
        }
        update_header_button();
    }
    invalidate_layout();
}

void DockableCollapsible::set_header_button_style(const DMButtonStyle* style) {
    const DMButtonStyle* resolved = style ? style : &DMStyles::HeaderButton();
    if (header_button_style_ == resolved) {
        return;
    }
    header_button_style_ = resolved;
    if (header_btn_) {
        header_btn_->set_style(header_button_style_);
        update_header_button();
    }
}

void DockableCollapsible::set_header_highlight_color(SDL_Color color) {
    header_highlight_override_ = color;
}

void DockableCollapsible::clear_header_highlight_color() {
    header_highlight_override_.reset();
}

void DockableCollapsible::set_close_button_enabled(bool enabled) {
    if (close_button_enabled_ == enabled) {
        return;
    }
    close_button_enabled_ = enabled;
    if (show_header_) {
        if (floatable_ || close_button_enabled_) {
            if (!close_btn_) {
                close_btn_ = std::make_unique<DMButton>(std::string(DMIcons::Close()), &DMStyles::DeleteButton(), DMButton::height(), DMButton::height());
            }
        } else {
            close_btn_.reset();
        }
    }
    invalidate_layout();
}

void DockableCollapsible::set_close_button_on_left(bool on_left) {
    if (close_button_on_left_ == on_left) {
        return;
    }
    close_button_on_left_ = on_left;
    invalidate_layout(true);
}

void DockableCollapsible::setLocked(bool locked) {
    apply_lock_state(locked, true, true);
}

void DockableCollapsible::onLockChanged(std::function<void(bool)> cb) {
    if (!cb) {
        return;
    }
    on_lock_changed_.push_back(std::move(cb));
}

void DockableCollapsible::set_scroll_enabled(bool enabled) {
    if (locked_) {
        log_locked_mutation("set_scroll_enabled");
        return;
    }
    scroll_enabled_ = enabled;
}

void DockableCollapsible::set_position(int x, int y) {
    set_position_internal(x, y, false);
}

void DockableCollapsible::set_position_from_layout_manager(int x, int y) {
    set_position_internal(x, y, true);
}

void DockableCollapsible::set_rect(const SDL_Rect& r) {
    rect_ = r;
    notify_layout_manager_geometry_changed();
    invalidate_layout();
}

void DockableCollapsible::set_floatable(bool floatable) {
    if (floatable_ == floatable) {
        return;
    }
    floatable_ = floatable;
    dragging_ = false;
    header_dragging_via_button_ = false;
    drag_exceeded_threshold_ = false;
    block_pointer_for(0);
    update_layout_manager_registration();
    notify_layout_manager_geometry_changed();
    invalidate_layout();
}

void DockableCollapsible::set_work_area(const SDL_Rect& area) {
    work_area_ = area;
    if (work_area_.w > 0) last_screen_w_ = work_area_.w;
    if (work_area_.h > 0) last_screen_h_ = work_area_.h;
    notify_layout_manager_geometry_changed();
    invalidate_layout();
}

void DockableCollapsible::set_available_height_override(int height) {
    if (locked_) {
        log_locked_mutation("set_available_height_override");
        return;
    }
    available_height_override_ = height;
    notify_layout_manager_geometry_changed();
    invalidate_layout(true);
}

void DockableCollapsible::set_cell_width(int w) {
    if (locked_) {
        log_locked_mutation("set_cell_width");
        return;
    }
    cell_width_ = std::max(40, w);
    notify_layout_manager_geometry_changed();
    invalidate_layout();
}

void DockableCollapsible::set_padding(int p) {
    if (locked_) {
        log_locked_mutation("set_padding");
        return;
    }
    padding_ = std::max(0, p);
    notify_layout_manager_geometry_changed();
    invalidate_layout();
}

void DockableCollapsible::set_row_gap(int g) {
    if (locked_) {
        log_locked_mutation("set_row_gap");
        return;
    }
    row_gap_ = std::max(0, g);
    notify_layout_manager_geometry_changed();
    invalidate_layout();
}

void DockableCollapsible::set_col_gap(int g) {
    if (locked_) {
        log_locked_mutation("set_col_gap");
        return;
    }
    col_gap_ = std::max(0, g);
    notify_layout_manager_geometry_changed();
    invalidate_layout();
}

void DockableCollapsible::set_visible_height(int h) {
    if (locked_) {
        log_locked_mutation("set_visible_height");
        return;
    }
    visible_height_ = std::max(0, h);
    notify_layout_manager_geometry_changed();
    invalidate_layout();
}

void DockableCollapsible::set_floating_content_width(int w) {
    if (locked_) {
        log_locked_mutation("set_floating_content_width");
        return;
    }
    int clamped = std::max(120, w);
    if (floating_content_width_ == clamped) {
        return;
    }
    floating_content_width_ = clamped;
    notify_layout_manager_geometry_changed();
    invalidate_layout();
}

void DockableCollapsible::set_position_internal(int x, int y, bool from_layout_manager) {
    if (!floatable_) {
        return;
    }
    rect_.x = x;
    rect_.y = y;

    if (from_layout_manager) {
        update_geometry_after_move();
        return;
    }

    notify_layout_manager_geometry_changed();
    clamp_to_bounds(last_screen_w_, last_screen_h_);
    invalidate_layout(true);
}

void DockableCollapsible::update_layout_manager_registration() {
    bool should_register = floatable_ && visible_;
    if (should_register) {
        if (!registered_with_layout_manager_) {
            FloatingPanelLayoutManager::instance().registerPanel(this);
            registered_with_layout_manager_ = true;
        }
    } else if (registered_with_layout_manager_) {
        FloatingPanelLayoutManager::instance().unregisterPanel(this);
        registered_with_layout_manager_ = false;
    }
}

void DockableCollapsible::notify_layout_manager_geometry_changed() const {
    if (!floatable_ || !registered_with_layout_manager_) {
        return;
    }
    FloatingPanelLayoutManager::instance().notifyPanelGeometryChanged(const_cast<DockableCollapsible*>(this));
}

void DockableCollapsible::notify_layout_manager_content_changed() const {
    if (!floatable_ || !registered_with_layout_manager_) {
        return;
    }
    FloatingPanelLayoutManager::instance().notifyPanelContentChanged(const_cast<DockableCollapsible*>(this));
}

void DockableCollapsible::block_pointer_for(Uint32 ms) const {
    if (ms == 0) {
        pointer_block_until_ms_ = 0;
        return;
    }
    pointer_block_until_ms_ = SDL_GetTicks() + ms;
}

bool DockableCollapsible::pointer_block_active() const {
    if (pointer_block_until_ms_ == 0) {
        return false;
    }
    Uint32 now = SDL_GetTicks();
    if (SDL_TICKS_PASSED(now, pointer_block_until_ms_)) {
        pointer_block_until_ms_ = 0;
        return false;
    }
    return true;
}

void DockableCollapsible::reset_scroll() const {
    if (locked_) {
        log_locked_mutation("reset_scroll");
        return;
    }
    scroll_ = 0;
    invalidate_layout(true);
}

void DockableCollapsible::force_pointer_ready() {
    block_pointer_for(0);
}

void DockableCollapsible::set_embedded_focus_state(bool focused) {
    if (embedded_focus_state_ == focused) {
        return;
    }
    embedded_focus_state_ = focused;
}

void DockableCollapsible::set_embedded_interaction_enabled(bool enabled) {
    if (embedded_interaction_enabled_ == enabled) {
        return;
    }
    embedded_interaction_enabled_ = enabled;
    if (!embedded_interaction_enabled_) {
        force_pointer_ready();
    }
}

void DockableCollapsible::invalidate_layout(bool geometry_only) const {
    if (!geometry_only) {
        needs_layout_ = true;
    }
    needs_geometry_ = true;
#if DM_FORCE_LAYOUT
    layout(last_screen_w_, last_screen_h_);
#endif
}

void DockableCollapsible::update(const Input& input, int screen_w, int screen_h) {
    if (!visible_) return;
    pointer_block_active();

#if DM_FORCE_LAYOUT
    LayoutTimingScope timing_scope(title_, true, true, false, true);
    layout(screen_w, screen_h);
#else
    bool resized = false;
    if (screen_w > 0 && screen_w != last_screen_w_) {
        resized = true;
    }
    if (screen_h > 0 && screen_h != last_screen_h_) {
        resized = true;
    }
    if (resized) {
        needs_geometry_ = true;
    }
    if (!layout_initialized_) {
        needs_layout_ = true;
        needs_geometry_ = true;
    }
    const bool layout_dirty = needs_layout_;
    const bool geometry_dirty = needs_geometry_;
    if (needs_layout_ || needs_geometry_) {
        LayoutTimingScope timing_scope(title_, layout_dirty, geometry_dirty, resized, false);
        layout(screen_w, screen_h);
    }
#endif

    if (!embedded_interaction_enabled_) {
        return;
    }

    if (locked_) {
        log_locked_mutation("update");
        return;
    }

    if (scroll_enabled_ && expanded_ && !locked_ && body_viewport_.w > 0 && body_viewport_.h > 0) {
        int mx = input.getX();
        int my = input.getY();
        if (mx >= body_viewport_.x && mx < body_viewport_.x + body_viewport_.w &&
            my >= body_viewport_.y && my < body_viewport_.y + body_viewport_.h) {
            int dy = input.getScrollY();
            if (dy != 0) {
                scroll_ -= dy * 40;
                scroll_ = std::max(0, std::min(max_scroll_, scroll_));
                invalidate_layout(true);
            }
        }
    }

    if (!show_header_) return;

    int mx = input.getX();
    int my = input.getY();
}

bool DockableCollapsible::handle_event(const SDL_Event& e) {
    if (!visible_ || !embedded_interaction_enabled_) return false;

    const bool pointer_event =
        (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEMOTION);
    const bool wheel_event = (e.type == SDL_MOUSEWHEEL);
    const bool slider_capture_active = DMWidgetsSliderScrollCaptured();
    SDL_Point pointer_pos{0, 0};
    bool pointer_blocked = pointer_block_active();
    if (pointer_event) {
        if (pointer_blocked) {
            return true;
        }
        if (e.type == SDL_MOUSEMOTION) {
            pointer_pos = SDL_Point{e.motion.x, e.motion.y};
        } else {
            pointer_pos = SDL_Point{e.button.x, e.button.y};
        }
    } else {
        if (wheel_event && pointer_blocked) {
            return true;
        }
    }

    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        SDL_Point p{e.button.x, e.button.y};
        const bool on_header_button = show_header_ && header_btn_ && SDL_PointInRect(&p, &header_rect_);
        const bool on_close = close_btn_ && SDL_PointInRect(&p, &close_rect_);
        const bool on_lock = lock_btn_ && SDL_PointInRect(&p, &lock_rect_);
        SDL_Rect drag_rect{ rect_.x + padding_, rect_.y + padding_, std::max(0, rect_.w - 2 * padding_), header_rect_.h };
        if (drag_rect.h <= 0) {
            drag_rect.h = DMButton::height();
        }
        const bool on_header_area = show_header_ && SDL_PointInRect(&p, &drag_rect);
        const bool on_custom_handle = (handle_rect_.w > 0 && handle_rect_.h > 0 && SDL_PointInRect(&p, &handle_rect_));
        if (floatable_ && (on_header_area || on_custom_handle) && !on_close && !on_lock) {
            dragging_ = true;
            header_dragging_via_button_ = on_header_button;
            drag_exceeded_threshold_ = false;
            drag_offset_.x = p.x - rect_.x;
            drag_offset_.y = p.y - rect_.y;
            drag_start_pointer_ = p;
            if (on_header_button && header_btn_) {
                header_btn_->handle_event(e);
            }
            return true;
        }
    }

    if (show_header_ && dragging_) {
        if (e.type == SDL_MOUSEMOTION) {
            SDL_Point current{e.motion.x, e.motion.y};
            if (!drag_exceeded_threshold_) {
                int dx = current.x - drag_start_pointer_.x;
                int dy = current.y - drag_start_pointer_.y;
                if (std::abs(dx) > kHeaderDragStartThreshold || std::abs(dy) > kHeaderDragStartThreshold) {
                    drag_exceeded_threshold_ = true;
                    FloatingDockableManager::instance().bring_to_front(this);
                }
            }
            if (drag_exceeded_threshold_) {
                rect_.x = current.x - drag_offset_.x;
                rect_.y = current.y - drag_offset_.y;
                clamp_to_bounds(last_screen_w_, last_screen_h_);
                invalidate_layout(true);
            }
            return true;
        }
        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            bool dragged_via_button = header_dragging_via_button_;
            bool drag_moved = drag_exceeded_threshold_;
            dragging_ = false;
            header_dragging_via_button_ = false;
            drag_exceeded_threshold_ = false;
            if (drag_moved) {
                notify_layout_manager_geometry_changed();
                FloatingPanelLayoutManager::instance().notifyPanelUserMoved(this);
                block_pointer_for(kPointerBlockAfterDragMs);
                invalidate_layout(true);
            }
            if (dragged_via_button && header_btn_) {
                header_btn_->handle_event(e);
                SDL_Point p{e.button.x, e.button.y};
                if (!drag_moved && SDL_PointInRect(&p, &header_rect_)) {
                    expanded_ = !expanded_;
                    update_header_button();
                    invalidate_layout();
                }
                return true;
            }
            return true;
        }
    }

    if (lock_btn_ && lock_btn_->handle_event(e)) {
        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            setLocked(!locked_);
        }
        return true;
    }

    if ((floatable_ || close_button_enabled_) && close_btn_ && close_btn_->handle_event(e)) {
        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            set_visible(false);
        }
        return true;
    }

    if (header_btn_) {
        if (header_btn_->handle_event(e)) {
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                expanded_ = !expanded_;
                update_header_button();
                invalidate_layout();
            }
            return true;
        }
    }

    if (locked_) {
        if (wheel_event) {
            SDL_Point wheel_point{0, 0};
            SDL_GetMouseState(&wheel_point.x, &wheel_point.y);
            if (SDL_PointInRect(&wheel_point, &body_viewport_)) {
                log_locked_mutation("handle_event.wheel");
                return true;
            }
            if (slider_capture_active) {
                return true;
            }
            return false;
        }

        if (pointer_event) {
            if (SDL_PointInRect(&pointer_pos, &body_viewport_)) {
                if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEMOTION) {
                    log_locked_mutation("handle_event.pointer");
                    return true;
                }
            }
            if (SDL_PointInRect(&pointer_pos, &rect_) && e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                return true;
            }
        }

        if (wheel_event && slider_capture_active) {
            return true;
        }

        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE && floatable_) {
            set_visible(false);
            return true;
        }

        return false;
    }

    if (expanded_ && scroll_enabled_ && wheel_event && !slider_capture_active) {
        SDL_Point mouse_point{0, 0};
        SDL_GetMouseState(&mouse_point.x, &mouse_point.y);
        if (SDL_PointInRect(&mouse_point, &body_viewport_)) {
            scroll_ -= e.wheel.y * 40;
            scroll_ = std::max(0, std::min(max_scroll_, scroll_));
            invalidate_layout(true);
            return true;
        }
    }

    bool forward_to_children = expanded_;
    if (forward_to_children && pointer_event) {
        if (SDL_PointInRect(&pointer_pos, &body_viewport_)) {
            forward_to_children = true;
        } else {
            bool dropdown_active = (DMDropdown::active_dropdown() != nullptr);
            forward_to_children = slider_capture_active || dropdown_active;
        }
    }

    if (forward_to_children) {
        for (auto& row : rows_) {
            for (auto* w : row) {
                if (w && w->handle_event(e)) {
                    return true;
                }
            }
        }
    }

    if (wheel_event && slider_capture_active) {
        return true;
    }

    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE && floatable_) {
        set_visible(false);
        return true;
    }

    if (pointer_event && SDL_PointInRect(&pointer_pos, &rect_)) {
        bool in_visible_region = false;
        if (show_header_ && SDL_PointInRect(&pointer_pos, &header_rect_)) {
            in_visible_region = true;
        }
        if (!in_visible_region && expanded_ && SDL_PointInRect(&pointer_pos, &body_viewport_)) {
            in_visible_region = true;
        }

        if (!in_visible_region) {
            return false;
        }

        switch (e.type) {
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
                return true;
            case SDL_MOUSEMOTION:
                return true;
            default:
                break;
        }
    }

    return false;
}
bool DockableCollapsible::is_point_inside(int x, int y) const {
    SDL_Point p{ x, y };
    return SDL_PointInRect(&p, &rect_);
}

void DockableCollapsible::render(SDL_Renderer* r) const {
    if (!r) return;
    if (!visible_) return;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    const SDL_Color& fill = DMStyles::PanelBG();
    SDL_Color header_highlight = header_highlight_override_.value_or(DMStyles::PanelHeader());
    const SDL_Color& border_shadow = DMStyles::Border();
    const float highlight_intensity = DMStyles::HighlightIntensity();
    const float shadow_intensity = DMStyles::ShadowIntensity();
    dm_draw::DrawBeveledRect( r, rect_, DMStyles::CornerRadius(), DMStyles::BevelDepth(), fill, header_highlight, border_shadow, false, highlight_intensity, shadow_intensity);
    if (rendering_embedded_ && embedded_focus_state_) {
        SDL_Rect focus_rect = rect_;
        focus_rect.x -= 2;
        focus_rect.y -= 2;
        focus_rect.w += 4;
        focus_rect.h += 4;
        focus_rect.w = std::max(0, focus_rect.w);
        focus_rect.h = std::max(0, focus_rect.h);
        const SDL_Color& focus_color = DMStyles::ButtonFocusOutline();
        dm_draw::DrawRoundedFocusRing(r, focus_rect, DMStyles::CornerRadius(), 2, focus_color);
    }

    if (header_btn_) header_btn_->render(r);
    if (lock_btn_) {
        lock_btn_->render(r);
        draw_lock_icon(r, lock_rect_, locked_);
    }
    if (close_btn_ && (floatable_ || close_button_enabled_)) close_btn_->render(r);

    if (!expanded_) return;

    SDL_Rect prev_clip;
    SDL_RenderGetClipRect(r, &prev_clip);
#if SDL_VERSION_ATLEAST(2,0,4)
    const SDL_bool was_clipping = SDL_RenderIsClipEnabled(r);
#else
    const SDL_bool was_clipping = (prev_clip.w != 0 || prev_clip.h != 0) ? SDL_TRUE : SDL_FALSE;
#endif
    SDL_RenderSetClipRect(r, &body_viewport_);

    for (auto& row : rows_) {
        for (auto* w : row) {
            if (w) w->render(r);
        }
    }
    render_content(r);

    if (locked_) {
        render_locked_children_overlay(r);
    }

    if (was_clipping == SDL_TRUE) {
        SDL_RenderSetClipRect(r, &prev_clip);
    } else {
        SDL_RenderSetClipRect(r, nullptr);
    }
}

void DockableCollapsible::render_locked_children_overlay(SDL_Renderer* r) const {
    if (!r || !locked_) {
        return;
    }

    SDL_Color widget_overlay{40, 40, 40, 140};
    SDL_SetRenderDrawColor(r, widget_overlay.r, widget_overlay.g, widget_overlay.b, widget_overlay.a);
    for (const auto& row : rows_) {
        for (const auto* w : row) {
            if (!w) {
                continue;
            }
            SDL_Rect widget_rect = w->rect();
            SDL_Rect clipped;
            if (SDL_IntersectRect(&widget_rect, &body_viewport_, &clipped)) {
                SDL_RenderFillRect(r, &clipped);
            }
        }
    }

    SDL_Color content_overlay{20, 20, 20, 110};
    SDL_SetRenderDrawColor(r, content_overlay.r, content_overlay.g, content_overlay.b, content_overlay.a);
    SDL_RenderFillRect(r, &body_viewport_);
}

void DockableCollapsible::layout() {
    layout(0,0);
}

void DockableCollapsible::layout(int screen_w, int screen_h) const {
    if (screen_w > 0) last_screen_w_ = screen_w;
    if (screen_h > 0) last_screen_h_ = screen_h;

    ensure_lock_state_initialized();
    ensure_lock_button();

    header_rect_ = SDL_Rect{ rect_.x + padding_, rect_.y + padding_, 0, show_header_ ? DMButton::height() : 0 };

    const bool show_close = floatable_ || close_button_enabled_;
    const bool show_lock = should_show_lock_button();
    const int button_width = DMButton::height();
    auto layout_rows = [this]() {
        std::vector<std::vector<Widget*>> layout_rows;
        layout_rows.reserve(rows_.size());
        for (const auto& row : rows_) {
            std::vector<Widget*> current;
            bool inserted_any = false;
            for (auto* w : row) {
                if (w && w->wants_full_row()) {
                    if (!current.empty()) {
                        layout_rows.push_back(current);
                        current.clear();
                    }
                    layout_rows.push_back({ w });
                    inserted_any = true;
                } else {
                    current.push_back(w);
                    inserted_any = true;
                }
            }
            if (!current.empty()) {
                layout_rows.push_back(std::move(current));
            } else if (!inserted_any) {
                layout_rows.push_back({});
            }
        }
        return layout_rows;
    }();

    auto finalize_layout = [this]() {
        needs_layout_ = false;
        needs_geometry_ = false;
        layout_initialized_ = true;
        for (const auto& row : rows_) {
            for (auto* w : row) {
                if (w) {
                    w->clear_layout_dirty_flags();
                }
            }
        }
        notify_layout_manager_content_changed();
};

    int header_total_w = 0;
    if (floatable_) {
        header_total_w = floating_content_width_;
        widest_row_w_ = 2 * padding_ + header_total_w;
        if (show_header_) {
            int available = header_total_w;
            if (show_close) available -= button_width;
            if (show_lock) available -= button_width;
            header_rect_.w = std::max(0, available);
            int header_x = header_rect_.x;
            if (show_close && close_button_on_left_) {
                close_rect_ = SDL_Rect{ header_x, header_rect_.y, button_width, button_width };
                header_rect_.x = header_x + button_width;
            } else {
                close_rect_ = SDL_Rect{0,0,0,0};
            }
            int next_x = header_rect_.x + header_rect_.w;
            if (show_lock) {
                lock_rect_ = SDL_Rect{ next_x, header_rect_.y, button_width, button_width };
                next_x += button_width;
            } else {
                lock_rect_ = SDL_Rect{0,0,0,0};
            }
            if (show_close && !close_button_on_left_) {
                close_rect_ = SDL_Rect{ next_x, header_rect_.y, button_width, button_width };
            }
        } else {
            header_rect_.w = header_total_w;
            close_rect_ = SDL_Rect{0,0,0,0};
            lock_rect_ = SDL_Rect{0,0,0,0};
        }
    } else {
        header_total_w = std::max(0, rect_.w - 2 * padding_);
        header_rect_.w = header_total_w;
        lock_rect_ = SDL_Rect{0,0,0,0};
        close_rect_ = SDL_Rect{0,0,0,0};
        if (show_header_) {
            const int header_y = rect_.y + padding_;
            int next_x = rect_.x + rect_.w - padding_;
            header_rect_.x = rect_.x + padding_;
            if (show_close && close_button_on_left_) {
                close_rect_ = SDL_Rect{ header_rect_.x, header_y, button_width, button_width };
                header_rect_.x += button_width;
                header_rect_.w = std::max(0, header_rect_.w - button_width);
            }
            if (show_lock) {
                lock_rect_ = SDL_Rect{ next_x - button_width, header_y, button_width, button_width };
                next_x -= button_width;
                header_rect_.w = std::max(0, header_rect_.w - button_width);
            }
            if (show_close && !close_button_on_left_) {
                close_rect_ = SDL_Rect{ next_x - button_width, header_y, button_width, button_width };
                next_x -= button_width;
                header_rect_.w = std::max(0, header_rect_.w - button_width);
            }
        }
    }

    if (header_btn_) header_btn_->set_rect(header_rect_);
    if (close_btn_) close_btn_->set_rect(close_rect_);
    if (lock_btn_) lock_btn_->set_rect(lock_rect_);
    update_header_button();
    update_lock_button();

    handle_rect_ = SDL_Rect{0, 0, 0, 0};

    int content_w = header_total_w;
    int header_gap = show_header_ ? DMSpacing::header_gap() : 0;
    int x0 = rect_.x + padding_;
    int y0 = rect_.y + padding_ + header_rect_.h + header_gap;

    row_heights_.clear();
    int computed_content_h = 0;
    for (const auto& row : layout_rows) {
        int n = (int)row.size();
        if (n <= 0) { row_heights_.push_back(0); continue; }
        int col_w = std::max(1, (content_w - (n - 1) * col_gap_) / n);
        int r_h = 0;
        for (auto* w : row) if (w) r_h = std::max(r_h, w->height_for_width(col_w));
        row_heights_.push_back(r_h);
        computed_content_h += r_h + row_gap_;
    }
    if (!row_heights_.empty()) computed_content_h -= row_gap_;
    if (!layout_rows.empty()) content_height_ = computed_content_h;

    if (!expanded_) {
        body_viewport_h_ = 0;
        body_viewport_   = SDL_Rect{ x0, y0, content_w, 0 };
        rect_.w = 2 * padding_ + content_w;
        rect_.h = padding_ + header_rect_.h + header_gap + padding_;
        max_scroll_ = 0;
        scroll_     = 0;
        if (floatable_) clamp_to_bounds(screen_w, screen_h);
        layout_custom_content(last_screen_w_, last_screen_h_);
        finalize_layout();
        return;
    }

    if (floatable_) {
        int available_h = available_height(screen_h);
        body_viewport_h_ = std::max(0, std::min(content_height_, available_h));
        max_scroll_      = std::max(0, content_height_ - body_viewport_h_);
        scroll_          = std::max(0, std::min(max_scroll_, scroll_));
    } else {
        int available_h = (available_height_override_ >= 0) ? available_height_override_ : content_height_;
        body_viewport_h_ = std::max(0, std::min(content_height_, available_h));
        max_scroll_      = std::max(0, content_height_ - body_viewport_h_);
        scroll_          = std::max(0, std::min(max_scroll_, scroll_));
    }

    body_viewport_ = SDL_Rect{ x0, y0, content_w, body_viewport_h_ };

    rect_.w = 2 * padding_ + content_w;
    rect_.h = padding_ + header_rect_.h + header_gap + body_viewport_h_ + padding_;

    int y = y0 - scroll_;
    for (size_t ri = 0; ri < layout_rows.size(); ++ri) {
        const auto& row = layout_rows[ri];
        int n = (int)row.size();
        if (n <= 0) continue;
        int col_w = std::max(1, (content_w - (n - 1) * col_gap_) / n);
        int h = row_heights_[ri];
        int x = x0;
        for (auto* w : row) {
            if (w) w->set_rect(SDL_Rect{ x, y, col_w, h });
            x += col_w + col_gap_;
        }
        y += h + row_gap_;
    }

    if (floatable_) clamp_to_bounds(screen_w, screen_h);
    layout_custom_content(last_screen_w_, last_screen_h_);
    finalize_layout();
}

void DockableCollapsible::update_header_button() const {
    if (!header_btn_) return;
    const std::string arrow = std::string(" ") + std::string(expanded_ ? DMIcons::CollapseExpanded() : DMIcons::CollapseCollapsed());
    header_btn_->set_text(title_ + arrow);
}

void DockableCollapsible::update_lock_button() const {
    if (!lock_btn_) {
        return;
    }
    if (locked_) {
        lock_btn_->set_style(&DMStyles::AccentButton());
    } else {
        lock_btn_->set_style(&DMStyles::HeaderButton());
    }
    lock_btn_->set_text("");
}

void DockableCollapsible::log_locked_mutation(std::string_view method) const {
    if (!locked_) {
        return;
    }
    std::string key(method);
    if (!locked_mutation_warnings_.insert(key).second) {
        return;
    }
    SDL_Log("DockableCollapsible[%s]: ignoring %s while locked", title_.c_str(), key.c_str());
}

int DockableCollapsible::compute_row_width(int num_cols) const {
    int inner = num_cols*cell_width_ + (num_cols-1)*col_gap_;
    return 2*padding_ + inner;
}

int DockableCollapsible::available_height(int screen_h) const {
    if (available_height_override_ >= 0) {
        return available_height_override_;
    }
    int bottom_space = DMSpacing::section_gap();
    int header_h = show_header_ ? DMButton::height() : 0;
    int header_gap = show_header_ ? DMSpacing::header_gap() : 0;
    int base_y = rect_.y + padding_ + header_h + header_gap;
    int computed;
    int area_h = (work_area_.w > 0 && work_area_.h > 0) ? work_area_.h : screen_h;
    int area_y = (work_area_.w > 0 && work_area_.h > 0) ? work_area_.y : 0;
    if (work_area_.w > 0 && work_area_.h > 0) {
        computed = area_y + area_h - bottom_space - base_y;
    } else {
        computed = screen_h - bottom_space - base_y;
    }
    int half_cap = std::max(0, area_h / 2);
    int capped = std::min(std::max(0, computed), half_cap);
    if (!floatable_) return visible_height_;
    return capped;
}

void DockableCollapsible::clamp_to_bounds(int screen_w, int screen_h) const {
    clamp_position_only(screen_w, screen_h);
    update_geometry_after_move();
}

void DockableCollapsible::clamp_position_only(int screen_w, int screen_h) const {
    SDL_Rect bounds = (work_area_.w > 0 && work_area_.h > 0)
                          ? work_area_
                          : SDL_Rect{0, 0, screen_w, screen_h};

    if (bounds.w <= 0 || bounds.h <= 0) {
        return;
    }

    if (rect_.w >= bounds.w) {
        rect_.x = bounds.x;
    } else {
        rect_.x = std::max(bounds.x, std::min(rect_.x, bounds.x + bounds.w - rect_.w));
    }

    if (rect_.h >= bounds.h) {
        rect_.y = bounds.y;
    } else {
        rect_.y = std::max(bounds.y, std::min(rect_.y, bounds.y + bounds.h - rect_.h));
    }
}

void DockableCollapsible::update_geometry_after_move() const {
    header_rect_.x = rect_.x + padding_;
    header_rect_.y = rect_.y + padding_;

    const bool show_close = floatable_ || close_button_enabled_;
    const bool show_lock = should_show_lock_button();
    if (show_header_) {
        if (floatable_) {
            int next_x = header_rect_.x + header_rect_.w;
            if (show_lock) {
                lock_rect_ = SDL_Rect{ next_x, header_rect_.y, DMButton::height(), DMButton::height() };
                next_x += DMButton::height();
            } else {
                lock_rect_ = SDL_Rect{0,0,0,0};
            }
            if (show_close) {
                close_rect_ = SDL_Rect{ next_x, header_rect_.y, DMButton::height(), DMButton::height() };
            } else {
                close_rect_ = SDL_Rect{0,0,0,0};
            }
        } else {
            int next_x = rect_.x + rect_.w - padding_;
            if (show_close) {
                close_rect_ = SDL_Rect{ next_x - DMButton::height(), rect_.y + padding_,
                                        DMButton::height(), DMButton::height() };
                next_x -= DMButton::height();
            } else {
                close_rect_ = SDL_Rect{0,0,0,0};
            }
            if (show_lock) {
                lock_rect_ = SDL_Rect{ next_x - DMButton::height(), rect_.y + padding_,
                                       DMButton::height(), DMButton::height() };
            } else {
                lock_rect_ = SDL_Rect{0,0,0,0};
            }
        }
    } else {
        close_rect_ = SDL_Rect{0,0,0,0};
        lock_rect_ = SDL_Rect{0,0,0,0};
    }

    if (header_btn_) header_btn_->set_rect(header_rect_);
    if (close_btn_ && show_close) close_btn_->set_rect(close_rect_);
    if (lock_btn_ && show_lock) lock_btn_->set_rect(lock_rect_);

    handle_rect_ = SDL_Rect{0,0,0,0};

    body_viewport_.x = rect_.x + padding_;
    body_viewport_.y = rect_.y + padding_ + header_rect_.h + (show_header_ ? DMSpacing::header_gap() : 0);
}

void DockableCollapsible::ensure_lock_state_initialized() const {
    if (lock_state_initialized_) {
        return;
    }
    lock_state_initialized_ = true;

    const std::string& key = lock_settings_key();
    if (key.empty()) {
        return;
    }
    bool stored = devmode::ui_settings::load_bool(key, locked_);
    apply_lock_state(stored, false, false);
}

void DockableCollapsible::ensure_lock_button() const {
    if (!should_show_lock_button()) {
        lock_btn_.reset();
        lock_rect_ = SDL_Rect{0,0,0,0};
        return;
    }
    if (!lock_btn_) {
        auto button = std::make_unique<DMButton>("", &DMStyles::HeaderButton(), DMButton::height(), DMButton::height());
        lock_btn_ = std::move(button);
        update_lock_button();
    }
}

const std::string& DockableCollapsible::lock_settings_key() const {
    if (lock_settings_key_cached_) {
        return lock_settings_key_cache_;
    }
    lock_settings_key_cached_ = true;
    lock_settings_key_cache_.clear();
    std::string_view ns = lock_settings_namespace();
    std::string_view id = lock_settings_id();
    if (ns.empty() || id.empty()) {
        return lock_settings_key_cache_;
    }
    lock_settings_key_cache_.reserve(ns.size() + id.size() + 12);
    lock_settings_key_cache_.append("dev_ui.lock.");
    lock_settings_key_cache_.append(ns.begin(), ns.end());
    lock_settings_key_cache_.push_back('.');
    lock_settings_key_cache_.append(id.begin(), id.end());
    return lock_settings_key_cache_;
}

bool DockableCollapsible::should_show_lock_button() const {
    if (!show_header_) {
        return false;
    }
    return !lock_settings_key().empty();
}

void DockableCollapsible::apply_lock_state(bool locked, bool allow_auto_collapse, bool persist) const {
    lock_state_initialized_ = true;
    if (locked_ == locked) {
        if (persist) {
            const std::string& key = lock_settings_key();
            if (!key.empty()) {
                devmode::ui_settings::save_bool(key, locked_);
            }
        }
        return;
    }

    locked_mutation_warnings_.clear();
    locked_ = locked;
    if (locked_ && allow_auto_collapse && expanded_) {
        const_cast<DockableCollapsible*>(this)->set_expanded(false);
    } else {
        update_header_button();
    }

    for (const auto& cb : on_lock_changed_) {
        if (cb) {
            cb(locked_);
        }
    }

    if (persist) {
        const std::string& key = lock_settings_key();
        if (!key.empty()) {
            devmode::ui_settings::save_bool(key, locked_);
        }
    }
}

void DockableCollapsible::capture_snapshot(EmbeddedSnapshot& out) const {
    out.rect = rect_;
    out.visible = visible_;
    out.expanded = expanded_;
    out.floatable = floatable_;
    out.scroll_enabled = scroll_enabled_;
    out.visible_height = visible_height_;
    out.available_height_override = available_height_override_;
    out.last_screen_w = last_screen_w_;
    out.last_screen_h = last_screen_h_;
}

void DockableCollapsible::apply_embedded_bounds(const SDL_Rect& bounds, int screen_w, int screen_h) {
    rect_ = bounds;
    floatable_ = false;
    scroll_enabled_ = false;
    visible_ = true;
    available_height_override_ = -1;
    needs_layout_ = true;
    needs_geometry_ = true;
    layout(screen_w > 0 ? screen_w : last_screen_w_, screen_h > 0 ? screen_h : last_screen_h_);
}

void DockableCollapsible::restore_snapshot(const EmbeddedSnapshot& snapshot) {
    rect_ = snapshot.rect;
    visible_ = snapshot.visible;
    expanded_ = snapshot.expanded;
    floatable_ = snapshot.floatable;
    scroll_enabled_ = snapshot.scroll_enabled;
    visible_height_ = snapshot.visible_height;
    available_height_override_ = snapshot.available_height_override;
    last_screen_w_ = snapshot.last_screen_w;
    last_screen_h_ = snapshot.last_screen_h;
    needs_layout_ = true;
    needs_geometry_ = true;
}

int DockableCollapsible::embedded_height(int width, int screen_h) {
    EmbeddedSnapshot snapshot;
    capture_snapshot(snapshot);
    SDL_Rect bounds = snapshot.rect;
    bounds.w = width;
    if (bounds.h <= 0) {
        bounds.h = snapshot.rect.h;
    }
    apply_embedded_bounds(bounds, width, screen_h);
    int measured = rect_.h;
    restore_snapshot(snapshot);
    return measured;
}

void DockableCollapsible::render_embedded(SDL_Renderer* renderer, const SDL_Rect& bounds, int screen_w, int screen_h) {
    if (!renderer) {
        return;
    }
    EmbeddedSnapshot snapshot;
    capture_snapshot(snapshot);
    apply_embedded_bounds(bounds, screen_w, screen_h);
    bool previous_rendering_state = rendering_embedded_;
    rendering_embedded_ = true;
    render(renderer);
    rendering_embedded_ = previous_rendering_state;
    restore_snapshot(snapshot);
}
