#include "DockableCollapsible.hpp"

#include "FloatingDockableManager.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "utils/input.hpp"

namespace {
    void draw_grip(SDL_Renderer* r, const SDL_Rect& area, SDL_Color col) {
        const int lines = 3;
        const int gap   = 3;
        int total_h = lines*1 + (lines-1)*gap;
        int start_y = area.y + (area.h - total_h)/2;
        SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
        for (int i=0;i<lines;++i) {
            int y = start_y + i*(1+gap);
            SDL_RenderDrawLine(r, area.x + 3, y, area.x + area.w - 3, y);
        }
    }
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
    header_btn_ = std::make_unique<DMButton>(title_, &DMStyles::HeaderButton(), 260, DMButton::height());
    close_btn_  = std::make_unique<DMButton>("X", &DMStyles::HeaderButton(), DMButton::height(), DMButton::height());
    padding_ = DMSpacing::panel_padding();
    row_gap_ = DMSpacing::item_gap();
    col_gap_ = DMSpacing::item_gap();
    update_header_button();
}

DockableCollapsible::~DockableCollapsible() = default;

void DockableCollapsible::set_visible(bool v) {
    if (visible_ == v) {
        return;
    }
    visible_ = v;
    if (!visible_) {
        dragging_ = false;
        FloatingDockableManager::instance().notify_panel_closed(this);
        if (on_close_) on_close_();
    }
}

void DockableCollapsible::set_rows(const Rows& rows) {
    rows_ = rows;
}

void DockableCollapsible::set_title(const std::string& title) {
    title_ = title;
    update_header_button();
}

void DockableCollapsible::set_expanded(bool e) {
    expanded_ = e;
    update_header_button();
}

void DockableCollapsible::set_show_header(bool show) {
    if (show_header_ == show) return;
    show_header_ = show;
    if (!show_header_) {
        expanded_ = true;
        header_btn_.reset();
        close_btn_.reset();
    } else {
        header_btn_ = std::make_unique<DMButton>(title_, &DMStyles::HeaderButton(), 260, DMButton::height());
        if (floatable_ || close_button_enabled_) {
            close_btn_ = std::make_unique<DMButton>("X", &DMStyles::HeaderButton(), DMButton::height(), DMButton::height());
        }
        update_header_button();
    }
    layout();
}

void DockableCollapsible::set_close_button_enabled(bool enabled) {
    if (close_button_enabled_ == enabled) {
        return;
    }
    close_button_enabled_ = enabled;
    if (show_header_) {
        if (floatable_ || close_button_enabled_) {
            if (!close_btn_) {
                close_btn_ = std::make_unique<DMButton>("X", &DMStyles::HeaderButton(), DMButton::height(), DMButton::height());
            }
        } else {
            close_btn_.reset();
        }
    }
    layout();
}

void DockableCollapsible::set_position(int x, int y) {
    if (!floatable_) return;
    rect_.x = x; rect_.y = y;
}

void DockableCollapsible::set_rect(const SDL_Rect& r) {
    rect_ = r;
    layout();
}

void DockableCollapsible::set_work_area(const SDL_Rect& area) {
    work_area_ = area;
}

void DockableCollapsible::update(const Input& input, int screen_w, int screen_h) {
    if (!visible_) return;
    layout(screen_w, screen_h);

    if (scroll_enabled_ && expanded_ && body_viewport_.w > 0 && body_viewport_.h > 0) {
        int mx = input.getX();
        int my = input.getY();
        if (mx >= body_viewport_.x && mx < body_viewport_.x + body_viewport_.w &&
            my >= body_viewport_.y && my < body_viewport_.y + body_viewport_.h) {
            int dy = input.getScrollY();
            if (dy != 0) {
                scroll_ -= dy * 40;
                scroll_ = std::max(0, std::min(max_scroll_, scroll_));
            }
        }
    }

    if (!show_header_) return;

    int mx = input.getX();
    int my = input.getY();
}

bool DockableCollapsible::handle_event(const SDL_Event& e) {
    if (!visible_) return false;

    const bool pointer_event =
        (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEMOTION);
    SDL_Point pointer_pos{0, 0};
    if (pointer_event) {
        if (e.type == SDL_MOUSEMOTION) {
            pointer_pos = SDL_Point{e.motion.x, e.motion.y};
        } else {
            pointer_pos = SDL_Point{e.button.x, e.button.y};
        }
    }

    // Start dragging from the header button or grip area before other header interactions
    if (show_header_ && e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        SDL_Point p{e.button.x, e.button.y};
        const bool on_header_button = header_btn_ && SDL_PointInRect(&p, &header_rect_);
        const bool on_handle = SDL_PointInRect(&p, &handle_rect_);
        if (floatable_ && (on_header_button || on_handle)) {
            dragging_ = true;
            drag_offset_.x = p.x - rect_.x;
            drag_offset_.y = p.y - rect_.y;
            header_dragging_via_button_ = on_header_button;
            header_btn_drag_moved_ = false;
            if (on_header_button && header_btn_) {
                header_btn_->handle_event(e);
            }
            return true;
        }
    }

    if (show_header_ && dragging_) {
        if (e.type == SDL_MOUSEMOTION) {
            rect_.x = e.motion.x - drag_offset_.x;
            rect_.y = e.motion.y - drag_offset_.y;
            if (header_dragging_via_button_) {
                header_btn_drag_moved_ = true;
            }
            return true;
        }
        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            bool dragged_via_button = header_dragging_via_button_;
            bool drag_moved = header_btn_drag_moved_;
            dragging_ = false;
            header_dragging_via_button_ = false;
            header_btn_drag_moved_ = false;
            if (dragged_via_button && header_btn_) {
                header_btn_->handle_event(e);
                SDL_Point p{e.button.x, e.button.y};
                if (!drag_moved && SDL_PointInRect(&p, &header_rect_)) {
                    expanded_ = !expanded_;
                    update_header_button();
                }
                return true;
            }
            return true;
        }
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
            }
            return true;
        }
    }

    if (expanded_ && scroll_enabled_ && e.type == SDL_MOUSEWHEEL) {
        SDL_Point mouse_point{0, 0};
        SDL_GetMouseState(&mouse_point.x, &mouse_point.y);
        if (SDL_PointInRect(&mouse_point, &body_viewport_)) {
            scroll_ -= e.wheel.y * 40;
            scroll_ = std::max(0, std::min(max_scroll_, scroll_));
            return true;
        }
    }

    bool forward_to_children = expanded_;
    if (forward_to_children && pointer_event) {
        forward_to_children = SDL_PointInRect(&pointer_pos, &body_viewport_);
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

    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE && floatable_) {
        set_visible(false);
        return true;
    }

    if (pointer_event && SDL_PointInRect(&pointer_pos, &rect_)) {
        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
            return true;
        }
    }

    return false;
}

bool DockableCollapsible::is_point_inside(int x, int y) const {
    SDL_Point p{ x, y };
    return SDL_PointInRect(&p, &rect_);
}

void DockableCollapsible::render(SDL_Renderer* r) const {
    if (!visible_) return;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_Color bg = DMStyles::PanelBG();
    SDL_Color border = DMStyles::Border();
    SDL_SetRenderDrawColor(r, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(r, &rect_);
    SDL_SetRenderDrawColor(r, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(r, &rect_);

    if (header_btn_) header_btn_->render(r);
    if (close_btn_ && (floatable_ || close_button_enabled_)) close_btn_->render(r);

    if (show_header_) {
        draw_grip(r, handle_rect_, DMStyles::Border());
    }

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

    if (was_clipping == SDL_TRUE) {
        SDL_RenderSetClipRect(r, &prev_clip);
    } else {
        SDL_RenderSetClipRect(r, nullptr);
    }
}

void DockableCollapsible::layout() {
    layout(0,0);
}

void DockableCollapsible::layout(int screen_w, int screen_h) const {
    header_rect_ = SDL_Rect{ rect_.x + padding_, rect_.y + padding_, 0, show_header_ ? DMButton::height() : 0 };

    const bool show_close = floatable_ || close_button_enabled_;
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

    if (floatable_) {
        widest_row_w_ = 2 * padding_;
        for (const auto& row : layout_rows) {
            int n = (int)row.size();
            if (n <= 0) continue;
            widest_row_w_ = std::max(widest_row_w_, compute_row_width(n));
        }
        int base_w = std::max(std::max(260, widest_row_w_ - 2 * padding_), 100);
        if (show_header_) {
            header_rect_.w = base_w;
            if (show_close) {
                close_rect_ = SDL_Rect{ header_rect_.x + header_rect_.w - DMButton::height(), header_rect_.y,
                                        DMButton::height(), DMButton::height() };
                header_rect_.w -= DMButton::height();
            } else {
                close_rect_ = SDL_Rect{0,0,0,0};
            }
        } else {
            header_rect_.w = rect_.w > 0 ? rect_.w - 2 * padding_ : base_w;
            close_rect_ = SDL_Rect{0,0,0,0};
        }
    } else {
        header_rect_.w = rect_.w - 2 * padding_;
        if (show_header_ && show_close) {
            close_rect_ = SDL_Rect{ rect_.x + rect_.w - padding_ - DMButton::height(), rect_.y + padding_,
                                    DMButton::height(), DMButton::height() };
            header_rect_.w = std::max(0, header_rect_.w - DMButton::height());
        } else {
            close_rect_ = SDL_Rect{0,0,0,0};
        }
    }

    if (header_btn_) header_btn_->set_rect(header_rect_);
    if (close_btn_ && show_close) close_btn_->set_rect(close_rect_);
    update_header_button();

    if (show_header_) {
        // Enlarge the draggable grip area to make it easier to grab
        int grip_w = std::max(32, std::min(80, std::max(1, header_rect_.w) / 3));
        handle_rect_ = SDL_Rect{ header_rect_.x, header_rect_.y, grip_w, header_rect_.h };
    } else {
        handle_rect_ = SDL_Rect{ 0, 0, 0, 0 };
    }

    int content_w = header_rect_.w;
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
        return;
    }

    if (floatable_) {
        int available_h = available_height(screen_h);
        body_viewport_h_ = std::max(0, std::min(content_height_, available_h));
        max_scroll_      = std::max(0, content_height_ - body_viewport_h_);
        scroll_          = std::max(0, std::min(max_scroll_, scroll_));
    } else {
        int available_h = (available_height_override_ >= 0)
                              ? available_height_override_
                              : content_height_;
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
}


void DockableCollapsible::update_header_button() const {
    if (!header_btn_) return;
    std::string arrow = expanded_ ? " \xE2\x96\xB2" : " \xE2\x96\xBC";
    header_btn_->set_text(title_ + arrow);
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
    const SDL_Rect bounds = (work_area_.w > 0 && work_area_.h > 0) ? work_area_ : SDL_Rect{0,0,screen_w,screen_h};
    rect_.x = std::max(bounds.x, std::min(rect_.x, bounds.x + bounds.w - rect_.w));
    rect_.y = std::max(bounds.y, std::min(rect_.y, bounds.y + bounds.h - rect_.h));
    header_rect_.x = rect_.x + padding_;
    header_rect_.y = rect_.y + padding_;
    const bool show_close = floatable_ || close_button_enabled_;
    if (floatable_ && show_header_) {
        if (show_close) {
            close_rect_ = SDL_Rect{ header_rect_.x + header_rect_.w - DMButton::height(), header_rect_.y,
                                    DMButton::height(), DMButton::height() };
            header_rect_.w -= DMButton::height();
        } else {
            close_rect_ = SDL_Rect{0,0,0,0};
        }
    } else if (!floatable_ && show_header_ && show_close) {
        close_rect_ = SDL_Rect{ rect_.x + rect_.w - padding_ - DMButton::height(), rect_.y + padding_,
                                DMButton::height(), DMButton::height() };
        header_rect_.w = std::max(0, header_rect_.w - DMButton::height());
    } else {
        close_rect_ = SDL_Rect{0,0,0,0};
    }
    if (header_btn_) header_btn_->set_rect(header_rect_);
    if (close_btn_ && show_close) close_btn_->set_rect(close_rect_);
    if (show_header_) {
        int grip_w = std::max(32, std::min(80, std::max(1, header_rect_.w) / 3));
        handle_rect_ = SDL_Rect{ header_rect_.x, header_rect_.y, grip_w, header_rect_.h };
    }
    body_viewport_.x = rect_.x + padding_;
    body_viewport_.y = rect_.y + padding_ + header_rect_.h + (show_header_ ? DMSpacing::header_gap() : 0);
}
