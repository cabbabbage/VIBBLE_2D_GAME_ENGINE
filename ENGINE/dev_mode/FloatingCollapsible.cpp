#include "FloatingCollapsible.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "utils/input.hpp"

namespace {
    // Draw a simple 3-line "grip" icon as the drag handle visual
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

    void draw_text(SDL_Renderer* r, const std::string& s, int x, int y, const DMLabelStyle& ls) {
        TTF_Font* f = TTF_OpenFont(ls.font_path.c_str(), ls.font_size);
        if (!f) return;
        SDL_Surface* surf = TTF_RenderUTF8_Blended(f, s.c_str(), ls.color);
        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
            if (tex) {
                SDL_Rect dst{ x, y, surf->w, surf->h };
                SDL_RenderCopy(r, tex, nullptr, &dst);
                SDL_DestroyTexture(tex);
            }
            SDL_FreeSurface(surf);
        }
        TTF_CloseFont(f);
    }
}

FloatingCollapsible::FloatingCollapsible(const std::string& title, int x, int y)
    : title_(title) {
    rect_.x = x; rect_.y = y;
    header_btn_ = std::make_unique<DMButton>(title_, &DMStyles::HeaderButton(), 260, DMButton::height());
    update_header_button();
}

FloatingCollapsible::~FloatingCollapsible() = default;

void FloatingCollapsible::set_rows(const Rows& rows) {
    rows_ = rows;
}

void FloatingCollapsible::set_expanded(bool e) {
    expanded_ = e;
    update_header_button();
}

void FloatingCollapsible::set_position(int x, int y) {
    rect_.x = x; rect_.y = y;
}

void FloatingCollapsible::set_work_area(const SDL_Rect& area) {
    work_area_ = area;
}

void FloatingCollapsible::update(const Input& input, int screen_w, int screen_h) {
    if (!visible_) return;
    layout(screen_w, screen_h);

    // Wheel scrolling via Input inside body viewport
    int mx = input.getX();
    int my = input.getY();
    if (expanded_ && body_viewport_.w > 0 && body_viewport_.h > 0) {
        if (mx >= body_viewport_.x && mx < body_viewport_.x + body_viewport_.w &&
            my >= body_viewport_.y && my < body_viewport_.y + body_viewport_.h) {
            int dy = input.getScrollY();
            if (dy != 0) {
                scroll_ -= dy * 40;
                scroll_ = std::max(0, std::min(max_scroll_, scroll_));
            }
        }
    }
}

bool FloatingCollapsible::handle_event(const SDL_Event& e) {
    if (!visible_) return false;

    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        SDL_Point p{ e.button.x, e.button.y };
        if (SDL_PointInRect(&p, &handle_rect_)) {
            dragging_ = true;
            drag_offset_.x = p.x - rect_.x;
            drag_offset_.y = p.y - rect_.y;
            return true;
        }
    } else if (e.type == SDL_MOUSEMOTION) {
        if (dragging_) {
            int nx = e.motion.x - drag_offset_.x;
            int ny = e.motion.y - drag_offset_.y;
            rect_.x = nx; rect_.y = ny;
            return true;
        }
    } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
        if (dragging_) { dragging_ = false; return true; }
    } else if (e.type == SDL_MOUSEWHEEL) {
    // Fallback scrolling from SDL events if inside body viewport
    int mx, my; 
    SDL_GetMouseState(&mx, &my);

    SDL_Point mouse_point{ mx, my };

    if (expanded_ && SDL_PointInRect(&mouse_point, &body_viewport_)) {
        scroll_ -= e.wheel.y * 40;
        scroll_ = std::max(0, std::min(max_scroll_, scroll_));
        return true;
    }

    }

    // Header toggle (avoid when dragging area was used)
    if (header_btn_) {
        // If mouse down occurred in header but not in handle, pass to button
        bool used = header_btn_->handle_event(e);
        if (used && e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            expanded_ = !expanded_;
            update_header_button();
            return true;
        }
        if (used) return true;
    }

    // Body children (only when expanded)
    if (expanded_) {
        if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEMOTION) {
            // Only forward events that land inside the body viewport
            SDL_Point p{
                e.type == SDL_MOUSEMOTION ? e.motion.x : e.button.x,
                e.type == SDL_MOUSEMOTION ? e.motion.y : e.button.y
            };
            if (!SDL_PointInRect(&p, &body_viewport_)) {
                return false;
            }
        }
        for (auto& row : rows_) {
            for (auto* w : row) {
                if (w && w->handle_event(e)) return true;
            }
        }
    }

    return false;
}

void FloatingCollapsible::render(SDL_Renderer* r) const {
    if (!visible_) return;

    // Header and panel background
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_Color bg = DMStyles::PanelBG();
    SDL_Color border = DMStyles::Border();
    SDL_SetRenderDrawColor(r, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(r, &rect_);
    SDL_SetRenderDrawColor(r, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(r, &rect_);

    if (header_btn_) header_btn_->render(r);

    // Drag handle grip
    draw_grip(r, handle_rect_, DMStyles::Border());

    if (!expanded_) return;

    // Clip body and render children
    SDL_Rect prev_clip;
    SDL_RenderGetClipRect(r, &prev_clip);
    // Track whether clipping was enabled so we can restore correctly
#if SDL_VERSION_ATLEAST(2,0,4)
    const SDL_bool was_clipping = SDL_RenderIsClipEnabled(r);
#else
    const SDL_bool was_clipping = (prev_clip.w != 0 || prev_clip.h != 0) ? SDL_TRUE : SDL_FALSE;
#endif
    SDL_RenderSetClipRect(r, &body_viewport_);

    // Children are assumed to have rects from layout(); simply render
    for (auto& row : rows_) {
        for (auto* w : row) {
            if (w) w->render(r);
        }
    }

    // Restore previous clip state (disable if it was disabled)
    if (was_clipping == SDL_TRUE) {
        SDL_RenderSetClipRect(r, &prev_clip);
    } else {
        SDL_RenderSetClipRect(r, nullptr);
    }
}

void FloatingCollapsible::layout(int screen_w, int screen_h) const {
    // Header rect occupies the top of the panel
    header_rect_ = SDL_Rect{ rect_.x + padding_, rect_.y + padding_, 0, DMButton::height() };

    // Compute width (widest row)
    widest_row_w_ = 2*padding_;
    for (const auto& row : rows_) {
        int n = (int)row.size();
        if (n <= 0) continue;
        widest_row_w_ = std::max(widest_row_w_, compute_row_width(n));
    }
    // Ensure header fits width
    header_rect_.w = std::max( std::max(260, widest_row_w_ - 2*padding_), 100 );

    // Update header button rect/text
    if (header_btn_) header_btn_->set_rect(header_rect_);
    update_header_button();

    // Drag handle area on the left of header
    handle_rect_ = SDL_Rect{ header_rect_.x, header_rect_.y, std::min(24, header_rect_.w/6), header_rect_.h };

    // Compute content geometry
    int content_w = header_rect_.w; // body matches header width inside padding
    int x0 = rect_.x + padding_;
    int y0 = rect_.y + padding_ + header_rect_.h + 8; // space below header

    // Compute per-row heights (based on tallest item)
    row_heights_.clear();
    content_height_ = 0;
    for (const auto& row : rows_) {
        int n = (int)row.size();
        if (n <= 0) { row_heights_.push_back(0); continue; }
        int col_w = std::max(1, (content_w - (n-1)*col_gap_)/n);
        int r_h = 0;
        for (auto* w : row) if (w) r_h = std::max(r_h, w->height_for_width(col_w));
        row_heights_.push_back(r_h);
        content_height_ += r_h + row_gap_;
    }
    if (!row_heights_.empty()) content_height_ -= row_gap_;

    // Determine viewport height from available screen/work area
    if (!expanded_) {
        // Collapsed: only show header area; no body/scroll
        body_viewport_h_ = 0;
        body_viewport_ = SDL_Rect{ x0, y0, content_w, 0 };
        rect_.w = 2*padding_ + content_w;
        rect_.h = padding_ + header_rect_.h + 8 + padding_;
        max_scroll_ = 0;
        scroll_ = 0;
        clamp_to_bounds(screen_w, screen_h);
        return;
    }

    // Expanded: cap visible body to half of the screen/work area height
    body_viewport_h_ = std::max(0, std::min(content_height_, available_height(screen_h)));
    body_viewport_ = SDL_Rect{ x0, y0, content_w, body_viewport_h_ };

    // Overall panel rect
    rect_.w = 2*padding_ + content_w;
    rect_.h = padding_ + header_rect_.h + 8 + body_viewport_h_ + padding_;

    // Scroll range
    max_scroll_ = std::max(0, content_height_ - body_viewport_h_);
    scroll_ = std::max(0, std::min(max_scroll_, scroll_));

    // Assign rects to each widget (apply scroll)
    int y = y0 - scroll_;
    for (size_t ri=0; ri<rows_.size(); ++ri) {
        const auto& row = rows_[ri];
        int n = (int)row.size();
        if (n <= 0) continue;
        int col_w = std::max(1, (content_w - (n-1)*col_gap_)/n);
        int h = row_heights_[ri];
        int x = x0;
        for (auto* w : row) {
            if (w) w->set_rect(SDL_Rect{ x, y, col_w, h });
            x += col_w + col_gap_;
        }
        y += h + row_gap_;
    }

    // Clamp position so panel remains on-screen
    clamp_to_bounds(screen_w, screen_h);
}

void FloatingCollapsible::update_header_button() const {
    if (!header_btn_) return;
    std::string arrow = expanded_ ? " \xE2\x96\xB2" /* ▲ */ : " \xE2\x96\xBC" /* ▼ */;
    header_btn_->set_text(title_ + arrow);
}

int FloatingCollapsible::compute_row_width(int num_cols) const {
    int inner = num_cols*cell_width_ + (num_cols-1)*col_gap_;
    return 2*padding_ + inner;
}

int FloatingCollapsible::available_height(int screen_h) const {
    int bottom_space = 16; // margin from bottom of screen or work area
    int base_y = rect_.y + padding_ + DMButton::height() + 8; // body top
    int computed;
    int area_h = (work_area_.w > 0 && work_area_.h > 0) ? work_area_.h : screen_h;
    int area_y = (work_area_.w > 0 && work_area_.h > 0) ? work_area_.y : 0;
    if (work_area_.w > 0 && work_area_.h > 0) {
        computed = area_y + area_h - bottom_space - base_y;
    } else {
        computed = screen_h - bottom_space - base_y;
    }
    // Cap to half of the available area height
    int half_cap = std::max(0, area_h / 2);
    int capped = std::min(std::max(0, computed), half_cap);
    return capped;
}

void FloatingCollapsible::clamp_to_bounds(int screen_w, int screen_h) const {
    const SDL_Rect bounds = (work_area_.w > 0 && work_area_.h > 0) ? work_area_ : SDL_Rect{0,0,screen_w,screen_h};
    rect_.x = std::max(bounds.x, std::min(rect_.x, bounds.x + bounds.w - rect_.w));
    rect_.y = std::max(bounds.y, std::min(rect_.y, bounds.y + bounds.h - rect_.h));
    // Recompute header/body rects after clamping
    header_rect_.x = rect_.x + padding_;
    header_rect_.y = rect_.y + padding_;
    if (header_btn_) header_btn_->set_rect(header_rect_);
    handle_rect_ = SDL_Rect{ header_rect_.x, header_rect_.y, std::min(24, header_rect_.w/6), header_rect_.h };
    body_viewport_.x = rect_.x + padding_;
    body_viewport_.y = rect_.y + padding_ + header_rect_.h + 8;
}
