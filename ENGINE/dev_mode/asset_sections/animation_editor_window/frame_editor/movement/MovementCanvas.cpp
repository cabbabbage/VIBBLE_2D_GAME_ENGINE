#include "MovementCanvas.hpp"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <limits>

#include "dm_styles.hpp"

namespace animation_editor {

namespace {

constexpr float kMinZoom = 0.2f;
constexpr float kMaxZoom = 6.0f;
constexpr int kPointRadius = 6;
constexpr int kHoverRadius = 12;

SDL_Color with_alpha(SDL_Color c, Uint8 alpha) {
    c.a = alpha;
    return c;
}

}

MovementCanvas::MovementCanvas() = default;

void MovementCanvas::set_bounds(const SDL_Rect& bounds) {
    bounds_ = bounds;
    fit_view_to_content();
}

void MovementCanvas::set_frames(const std::vector<MovementFrame>& frames, bool preserve_view) {
    frames_ = frames;
    if (frames_.empty()) {
        frames_.push_back(MovementFrame{});
    }
    if (!frames_.empty()) {
        frames_[0].dx = 0.0f;
        frames_[0].dy = 0.0f;
    }
    selected_index_ = std::clamp(selected_index_, 0, static_cast<int>(frames_.size()) - 1);
    rebuild_path();
    if (!preserve_view) {
        fit_view_to_content();
    }
}

void MovementCanvas::set_selected_index(int index) {
    if (frames_.empty()) {
        selected_index_ = 0;
        return;
    }
    index = std::clamp(index, 0, static_cast<int>(frames_.size()) - 1);
    if (index == selected_index_) return;
    selected_index_ = index;
}

void MovementCanvas::update() { update_selection_from_mouse(); }

void MovementCanvas::render(SDL_Renderer* renderer) const {
    if (!renderer) return;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    const SDL_Color bg = DMStyles::PanelBG();
    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, 220);
    SDL_RenderFillRect(renderer, &bounds_);

    SDL_Color border = DMStyles::AccentButton().hover_bg;
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, 255);
    SDL_RenderDrawRect(renderer, &bounds_);

    const float scale = pixels_per_unit_ * zoom_;
    const SDL_FPoint center_px{bounds_.x + bounds_.w / 2.0f, bounds_.y + bounds_.h / 2.0f};

    SDL_FPoint origin_screen = world_to_screen(SDL_FPoint{0.0f, 0.0f});
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, 40);
    SDL_RenderDrawLine(renderer, bounds_.x, static_cast<int>(origin_screen.y), bounds_.x + bounds_.w, static_cast<int>(origin_screen.y));
    SDL_RenderDrawLine(renderer, static_cast<int>(origin_screen.x), bounds_.y, static_cast<int>(origin_screen.x), bounds_.y + bounds_.h);

    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, 25);
    if (scale >= 8.0f) {
        const float units_visible_x = bounds_.w / scale;
        const float units_visible_y = bounds_.h / scale;
        const int start_x = static_cast<int>(std::floor(center_world_.x - units_visible_x));
        const int end_x = static_cast<int>(std::ceil(center_world_.x + units_visible_x));
        const int start_y = static_cast<int>(std::floor(center_world_.y - units_visible_y));
        const int end_y = static_cast<int>(std::ceil(center_world_.y + units_visible_y));
        for (int x = start_x; x <= end_x; ++x) {
            SDL_FPoint a = world_to_screen(SDL_FPoint{static_cast<float>(x), static_cast<float>(start_y - 1)});
            SDL_FPoint b = world_to_screen(SDL_FPoint{static_cast<float>(x), static_cast<float>(end_y + 1)});
            SDL_RenderDrawLine(renderer, static_cast<int>(a.x), static_cast<int>(a.y), static_cast<int>(b.x), static_cast<int>(b.y));
        }
        for (int y = start_y; y <= end_y; ++y) {
            SDL_FPoint a = world_to_screen(SDL_FPoint{static_cast<float>(start_x - 1), static_cast<float>(y)});
            SDL_FPoint b = world_to_screen(SDL_FPoint{static_cast<float>(end_x + 1), static_cast<float>(y)});
            SDL_RenderDrawLine(renderer, static_cast<int>(a.x), static_cast<int>(a.y), static_cast<int>(b.x), static_cast<int>(b.y));
        }
    }

    SDL_Color path_color = DMStyles::AccentButton().bg;
    SDL_SetRenderDrawColor(renderer, path_color.r, path_color.g, path_color.b, 200);
    for (size_t i = 1; i < positions_.size(); ++i) {
        SDL_FPoint prev = world_to_screen(positions_[i - 1]);
        SDL_FPoint curr = world_to_screen(positions_[i]);
        SDL_RenderDrawLine(renderer, static_cast<int>(std::round(prev.x)), static_cast<int>(std::round(prev.y)), static_cast<int>(std::round(curr.x)), static_cast<int>(std::round(curr.y)));
    }

    for (size_t i = 0; i < positions_.size(); ++i) {
        SDL_FPoint screen = world_to_screen(positions_[i]);
        SDL_Rect marker{static_cast<int>(std::round(screen.x)) - kPointRadius,
                        static_cast<int>(std::round(screen.y)) - kPointRadius, kPointRadius * 2, kPointRadius * 2};

        SDL_Color fill = DMStyles::ListButton().bg;
        if (static_cast<int>(i) == selected_index_) {
            fill = DMStyles::AccentButton().hover_bg;
        } else if (static_cast<int>(i) == hovered_index_) {
            fill = DMStyles::AccentButton().bg;
        }
        SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, 230);
        SDL_RenderFillRect(renderer, &marker);

        SDL_Color outline = DMStyles::ListButton().border;
        SDL_SetRenderDrawColor(renderer, outline.r, outline.g, outline.b, 255);
        SDL_RenderDrawRect(renderer, &marker);

        if (frames_[i].resort_z) {
            SDL_Color indicator = with_alpha(DMStyles::DeleteButton().bg, 220);
            SDL_Rect flag{marker.x, marker.y - 6, marker.w, 4};
            SDL_SetRenderDrawColor(renderer, indicator.r, indicator.g, indicator.b, indicator.a);
            SDL_RenderFillRect(renderer, &flag);
        }
    }
}

bool MovementCanvas::handle_event(const SDL_Event& e) {
    if (frames_.empty()) return false;

    auto within_bounds = [&](int x, int y) {
        SDL_Point p{x, y};
        return SDL_PointInRect(&p, &bounds_) != 0;
};

    switch (e.type) {
        case SDL_MOUSEMOTION: {
            last_mouse_.x = e.motion.x;
            last_mouse_.y = e.motion.y;
            bool inside = within_bounds(e.motion.x, e.motion.y);

            if (dragging_frame_ && selected_index_ > 0) {
                const float scale = pixels_per_unit_ * zoom_;
                SDL_Point current{e.motion.x, e.motion.y};
                float dx_units = (current.x - drag_last_mouse_.x) / scale;
                float dy_units = -(current.y - drag_last_mouse_.y) / scale;
                drag_target_world_.x += dx_units;
                drag_target_world_.y += dy_units;
                drag_last_mouse_ = current;

                const SDL_FPoint prev_world = positions_[selected_index_ - 1];
                frames_[selected_index_].dx = drag_target_world_.x - prev_world.x;
                frames_[selected_index_].dy = drag_target_world_.y - prev_world.y;
                rebuild_path();
            } else if (panning_) {
                pan_view(static_cast<float>(e.motion.xrel), static_cast<float>(e.motion.yrel));
            }

            update_selection_from_mouse();
            return dragging_frame_ || panning_ || inside;
        }
        case SDL_MOUSEBUTTONDOWN: {
            if (!within_bounds(e.button.x, e.button.y)) {
                return false;
            }
            last_mouse_.x = e.button.x;
            last_mouse_.y = e.button.y;
            if (e.button.button == SDL_BUTTON_LEFT) {
                update_selection_from_mouse();
                if (hovered_index_ >= 0) {
                    selected_index_ = hovered_index_;
                    if (selected_index_ > 0) {
                        dragging_frame_ = true;
                        drag_last_mouse_ = SDL_Point{e.button.x, e.button.y};
                        drag_target_world_ = positions_[selected_index_];
                    }
                }
                return true;
            }
            if (e.button.button == SDL_BUTTON_RIGHT || e.button.button == SDL_BUTTON_MIDDLE) {
                panning_ = true;
                drag_last_mouse_ = SDL_Point{e.button.x, e.button.y};
                return true;
            }
            break;
        }
        case SDL_MOUSEBUTTONUP: {
            if (e.button.button == SDL_BUTTON_LEFT && dragging_frame_) {
                dragging_frame_ = false;
                return true;
            }
            if ((e.button.button == SDL_BUTTON_RIGHT || e.button.button == SDL_BUTTON_MIDDLE) && panning_) {
                panning_ = false;
                return true;
            }
            break;
        }
        case SDL_MOUSEWHEEL: {
            if (!within_bounds(last_mouse_.x, last_mouse_.y)) {
                return false;
            }
            apply_zoom(static_cast<float>(e.wheel.y));
            return true;
        }
        default:
            break;
    }

    return false;
}

void MovementCanvas::rebuild_path() {
    positions_.clear();
    if (frames_.empty()) return;

    SDL_FPoint current{0.0f, 0.0f};
    for (size_t i = 0; i < frames_.size(); ++i) {
        if (i == 0) {
            current = SDL_FPoint{0.0f, 0.0f};
        } else {
            current.x += frames_[i].dx;
            current.y += frames_[i].dy;
        }
        positions_.push_back(current);
    }
    hovered_index_ = std::clamp(hovered_index_, -1, static_cast<int>(positions_.size()) - 1);
}

void MovementCanvas::fit_view_to_content() {
    if (positions_.empty() || bounds_.w <= 0 || bounds_.h <= 0) {
        center_world_ = SDL_FPoint{0.0f, 0.0f};
        zoom_ = 1.0f;
        return;
    }

    float min_x = std::numeric_limits<float>::max();
    float max_x = std::numeric_limits<float>::lowest();
    float min_y = std::numeric_limits<float>::max();
    float max_y = std::numeric_limits<float>::lowest();
    for (const auto& pos : positions_) {
        min_x = std::min(min_x, pos.x);
        max_x = std::max(max_x, pos.x);
        min_y = std::min(min_y, pos.y);
        max_y = std::max(max_y, pos.y);
    }

    if (std::isinf(min_x) || std::isinf(min_y)) {
        center_world_ = SDL_FPoint{0.0f, 0.0f};
        zoom_ = 1.0f;
        return;
    }

    center_world_.x = (min_x + max_x) * 0.5f;
    center_world_.y = (min_y + max_y) * 0.5f;

    const float extent_x = std::max(1.0f, max_x - min_x);
    const float extent_y = std::max(1.0f, max_y - min_y);
    const float margin = 0.5f;
    const float total_extent_x = extent_x + margin;
    const float total_extent_y = extent_y + margin;

    const float scale_x = bounds_.w / (total_extent_x * pixels_per_unit_);
    const float scale_y = bounds_.h / (total_extent_y * pixels_per_unit_);
    const float fit_zoom = std::max(kMinZoom, std::min(kMaxZoom, std::min(scale_x, scale_y)));
    zoom_ = std::clamp(fit_zoom, kMinZoom, kMaxZoom);
}

void MovementCanvas::pan_view(float delta_x, float delta_y) {
    const float scale = pixels_per_unit_ * zoom_;
    if (scale <= 0.0f) return;
    center_world_.x -= delta_x / scale;
    center_world_.y += delta_y / scale;
}

void MovementCanvas::apply_zoom(float scale_delta) {
    if (scale_delta == 0.0f) return;
    const float factor = (scale_delta > 0.0f) ? 1.1f : (1.0f / 1.1f);
    SDL_FPoint anchor_world = screen_to_world(last_mouse_);
    zoom_ = std::clamp(zoom_ * factor, kMinZoom, kMaxZoom);
    SDL_FPoint new_anchor_world = screen_to_world(last_mouse_);
    center_world_.x += anchor_world.x - new_anchor_world.x;
    center_world_.y += anchor_world.y - new_anchor_world.y;
}

void MovementCanvas::update_selection_from_mouse() {
    if (!SDL_PointInRect(&last_mouse_, &bounds_) || positions_.empty()) {
        hovered_index_ = -1;
        return;
    }

    float best_dist_sq = static_cast<float>(kHoverRadius * kHoverRadius);
    hovered_index_ = -1;
    for (size_t i = 0; i < positions_.size(); ++i) {
        SDL_FPoint screen = world_to_screen(positions_[i]);
        float dx = screen.x - static_cast<float>(last_mouse_.x);
        float dy = screen.y - static_cast<float>(last_mouse_.y);
        float dist_sq = dx * dx + dy * dy;
        if (dist_sq <= best_dist_sq) {
            best_dist_sq = dist_sq;
            hovered_index_ = static_cast<int>(i);
        }
    }
}

SDL_FPoint MovementCanvas::world_to_screen(const SDL_FPoint& world) const {
    const float scale = pixels_per_unit_ * zoom_;
    SDL_FPoint center_px{bounds_.x + bounds_.w / 2.0f, bounds_.y + bounds_.h / 2.0f};
    return SDL_FPoint{center_px.x + (world.x - center_world_.x) * scale,
                      center_px.y - (world.y - center_world_.y) * scale};
}

SDL_FPoint MovementCanvas::screen_to_world(SDL_Point screen) const {
    const float scale = pixels_per_unit_ * zoom_;
    SDL_FPoint center_px{bounds_.x + bounds_.w / 2.0f, bounds_.y + bounds_.h / 2.0f};
    if (scale <= 0.0f) {
        return center_world_;
    }
    return SDL_FPoint{(static_cast<float>(screen.x) - center_px.x) / scale + center_world_.x,
                      -(static_cast<float>(screen.y) - center_px.y) / scale + center_world_.y};
}

}

