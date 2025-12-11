#include "MovementCanvas.hpp"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <limits>

#include "dm_styles.hpp"
#include "draw_utils.hpp"
#include "../../PreviewProvider.hpp"
#include "utils/grid.hpp"

namespace animation_editor {

namespace {

constexpr float kMinZoom = 0.125f;
constexpr float kMaxZoom = 32.0f;
constexpr int kPointRadius = 6;
constexpr int kHoverRadius = 12;
constexpr int kMajorGridInterval = 32;
constexpr Uint8 kMinorGridAlpha = 22;
constexpr Uint8 kMajorGridAlpha = 55;
constexpr Uint8 kAxisAlpha = 170;

SDL_Color with_alpha(SDL_Color c, Uint8 alpha) {
    c.a = alpha;
    return c;
}

SDL_FPoint round_point_to_pixel(SDL_FPoint p) {
    return SDL_FPoint{static_cast<float>(std::round(p.x)), static_cast<float>(std::round(p.y))};
}

float round_delta_to_pixel(float value) {
    if (!std::isfinite(value)) {
        return 0.0f;
    }
    return static_cast<float>(std::round(value));
}

SDL_FPoint snap_to_resolution(SDL_FPoint p, int resolution_r) {
    if (resolution_r < 0) {

        return SDL_FPoint{ static_cast<float>(std::round(p.x)), static_cast<float>(std::round(p.y)) };
    }
    SDL_Point world_px{ static_cast<int>(std::lround(p.x)), static_cast<int>(std::lround(p.y)) };
    SDL_Point snapped = vibble::grid::snap_world_to_vertex(world_px, vibble::grid::clamp_resolution(resolution_r));
    return SDL_FPoint{ static_cast<float>(snapped.x), static_cast<float>(snapped.y) };
}

static SDL_FPoint bezier2_point(const SDL_FPoint& p0, const SDL_FPoint& p1, const SDL_FPoint& p2, double t) {
    const double u = 1.0 - t;
    const double uu = u * u;
    const double tt = t * t;
    return SDL_FPoint{
        static_cast<float>(uu * p0.x + 2.0 * u * t * p1.x + tt * p2.x), static_cast<float>(uu * p0.y + 2.0 * u * t * p1.y + tt * p2.y) };
}

static std::vector<SDL_FPoint> bezier2_sampled_polyline(const SDL_FPoint& p0,
                                                        const SDL_FPoint& p1,
                                                        const SDL_FPoint& p2,
                                                        int samples) {
    samples = std::max(2, samples);
    std::vector<SDL_FPoint> pts;
    pts.reserve(samples);
    for (int i = 0; i < samples; ++i) {
        double t = (samples == 1) ? 0.0 : static_cast<double>(i) / static_cast<double>(samples - 1);
        pts.push_back(bezier2_point(p0, p1, p2, t));
    }
    return pts;
}

static std::vector<double> cumulative_lengths(const std::vector<SDL_FPoint>& polyline) {
    std::vector<double> acc;
    acc.reserve(polyline.size());
    double total = 0.0;
    for (size_t i = 0; i < polyline.size(); ++i) {
        if (i == 0) {
            acc.push_back(0.0);
        } else {
            const double dx = static_cast<double>(polyline[i].x) - static_cast<double>(polyline[i - 1].x);
            const double dy = static_cast<double>(polyline[i].y) - static_cast<double>(polyline[i - 1].y);
            total += std::sqrt(dx * dx + dy * dy);
            acc.push_back(total);
        }
    }
    return acc;
}

static SDL_FPoint interpolate_along_polyline(const std::vector<SDL_FPoint>& polyline,
                                             const std::vector<double>& cumlen,
                                             double distance) {
    if (polyline.empty()) return SDL_FPoint{0.0f, 0.0f};
    if (distance <= 0.0) return polyline.front();
    const double total = cumlen.back();
    if (distance >= total) return polyline.back();

    size_t lo = 0, hi = cumlen.size() - 1;
    while (hi - lo > 1) {
        size_t mid = (lo + hi) / 2;
        if (cumlen[mid] < distance) lo = mid; else hi = mid;
    }
    const double seg_len = cumlen[hi] - cumlen[lo];
    const double seg_t = (seg_len > 0.0) ? (distance - cumlen[lo]) / seg_len : 0.0;
    const SDL_FPoint a = polyline[lo];
    const SDL_FPoint b = polyline[hi];
    return SDL_FPoint{ static_cast<float>(a.x + (b.x - a.x) * seg_t),
                       static_cast<float>(a.y + (b.y - a.y) * seg_t) };
}

}

MovementCanvas::MovementCanvas() : zoom_(16.0f) {}

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
    drag_base_positions_.clear();
    dragging_frame_ = false;
    selected_index_ = std::clamp(selected_index_, 0, static_cast<int>(frames_.size()) - 1);
    rebuild_path();
    if (selected_index_ >= 0 && selected_index_ < static_cast<int>(positions_.size())) {
        drag_target_world_ = positions_[selected_index_];
    } else {
        drag_target_world_ = SDL_FPoint{0.0f, 0.0f};
    }
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

    dm_draw::DrawBeveledRect( renderer, bounds_, DMStyles::CornerRadius(), DMStyles::BevelDepth(), DMStyles::PanelBG(), DMStyles::HighlightColor(), DMStyles::ShadowColor(), false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());

    render_pixel_grid(renderer);

    if (preview_provider_) {
        SDL_Texture* tex = nullptr;
        if (!animation_id_.empty()) {
            tex = preview_provider_->get_frame_texture(renderer, animation_id_, std::max(0, selected_index_));
        }
        if (tex) {
            int tw = 0, th = 0;
            if (SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th) == 0 && tw > 0 && th > 0) {
                const float scale = pixels_per_unit_ * zoom_;
                const float scale_factor = (base_scale_percentage_ <= 0.0f) ? 1.0f : (base_scale_percentage_ / 100.0f);
                const float dst_w_px = static_cast<float>(tw) * scale_factor * scale;
                const float dst_h_px = static_cast<float>(th) * scale_factor * scale;
                SDL_FPoint anchor_world = SDL_FPoint{0.0f, 0.0f};
                if (anchor_follows_movement_ &&
                    selected_index_ >= 0 && selected_index_ < static_cast<int>(positions_.size())) {
                    anchor_world = positions_[selected_index_];
                }
                SDL_FPoint anchor_screen = world_to_screen(anchor_world);

                const int left   = static_cast<int>(std::round(anchor_screen.x - dst_w_px * 0.5f));
                const int top    = static_cast<int>(std::round(anchor_screen.y - dst_h_px));
                SDL_Rect dst{ left, top, static_cast<int>(std::round(dst_w_px)), static_cast<int>(std::round(dst_h_px)) };
                SDL_RenderCopy(renderer, tex, nullptr, &dst);
            }
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
        const SDL_Color fill_color{fill.r, fill.g, fill.b, 230};
        const SDL_Color outline = DMStyles::ListButton().border;
        const int radius = std::min(DMStyles::CornerRadius(), std::min(marker.w, marker.h) / 2);
        const int bevel = std::min(DMStyles::BevelDepth(), std::max(0, std::min(marker.w, marker.h) / 2));
        dm_draw::DrawBeveledRect( renderer, marker, radius, bevel, fill_color, fill_color, fill_color, false, 0.0f, 0.0f);

        dm_draw::DrawRoundedOutline( renderer, marker, radius, 1, outline);

        if (frames_[i].resort_z) {
            SDL_Color indicator = with_alpha(DMStyles::DeleteButton().bg, 220);
            SDL_Rect flag{marker.x, marker.y - 6, marker.w, 4};
            SDL_SetRenderDrawColor(renderer, indicator.r, indicator.g, indicator.b, indicator.a);
            SDL_RenderFillRect(renderer, &flag);
        }
    }
}

SDL_FPoint snap_to_grid_resolution(SDL_FPoint p) {

    if (kMajorGridInterval <= 1) {
        return SDL_FPoint{ static_cast<float>(std::round(p.x)), static_cast<float>(std::round(p.y)) };
    }
    const float step = static_cast<float>(kMajorGridInterval);
    float sx = std::round(p.x / step) * step;
    float sy = std::round(p.y / step) * step;
    return SDL_FPoint{ sx, sy };
}

void MovementCanvas::render_background(SDL_Renderer* renderer) const {
    if (!renderer) return;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    dm_draw::DrawBeveledRect(renderer, bounds_, DMStyles::CornerRadius(), DMStyles::BevelDepth(), DMStyles::PanelBG(), DMStyles::HighlightColor(), DMStyles::ShadowColor(), false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());

    render_pixel_grid(renderer);

    if (preview_provider_) {
        SDL_Texture* tex = nullptr;
        if (!animation_id_.empty()) {
            tex = preview_provider_->get_frame_texture(renderer, animation_id_, std::max(0, selected_index_));
        }
        if (tex) {
            int tw = 0, th = 0;
            if (SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th) == 0 && tw > 0 && th > 0) {
                const float scale = pixels_per_unit_ * zoom_;
                const float scale_factor = (base_scale_percentage_ <= 0.0f) ? 1.0f : (base_scale_percentage_ / 100.0f);
                const float dst_w_px = static_cast<float>(tw) * scale_factor * scale;
                const float dst_h_px = static_cast<float>(th) * scale_factor * scale;
                SDL_FPoint anchor_world = SDL_FPoint{0.0f, 0.0f};
                if (anchor_follows_movement_ &&
                    selected_index_ >= 0 && selected_index_ < static_cast<int>(positions_.size())) {
                    anchor_world = positions_[selected_index_];
                }
                SDL_FPoint anchor_screen = world_to_screen(anchor_world);
                const int left = static_cast<int>(std::round(anchor_screen.x - dst_w_px * 0.5f));
                const int top = static_cast<int>(std::round(anchor_screen.y - dst_h_px));
                SDL_Rect dst{left, top, static_cast<int>(std::round(dst_w_px)), static_cast<int>(std::round(dst_h_px))};
                SDL_RenderCopy(renderer, tex, nullptr, &dst);
            }
        }
    }
}

void MovementCanvas::render_pixel_grid(SDL_Renderer* renderer) const {
    if (!renderer) return;
    if (bounds_.w <= 0 || bounds_.h <= 0) return;

    const float scale = pixels_per_unit_ * zoom_;
    if (!std::isfinite(scale) || scale <= 0.0f) return;

    const SDL_FPoint center_px{bounds_.x + bounds_.w / 2.0f, bounds_.y + bounds_.h / 2.0f};
    const float half_units_x = bounds_.w / (2.0f * scale);
    const float half_units_y = bounds_.h / (2.0f * scale);

    const int start_x = static_cast<int>(std::floor(center_world_.x - half_units_x)) - 1;
    const int end_x = static_cast<int>(std::ceil(center_world_.x + half_units_x)) + 1;
    const int start_y = static_cast<int>(std::floor(center_world_.y - half_units_y)) - 1;
    const int end_y = static_cast<int>(std::ceil(center_world_.y + half_units_y)) + 1;

    const float left = static_cast<float>(bounds_.x);
    const float right = static_cast<float>(bounds_.x + bounds_.w);
    const float top = static_cast<float>(bounds_.y);
    const float bottom = static_cast<float>(bounds_.y + bounds_.h);

    SDL_Color base = DMStyles::AccentButton().hover_bg;

    auto draw_vertical = [&](int x, Uint8 alpha) {
        float screen_x = center_px.x + (static_cast<float>(x) - center_world_.x) * scale;
        if (screen_x < left - 1.0f || screen_x > right + 1.0f) return;
        SDL_Color color = with_alpha(base, alpha);
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
        const int sx = static_cast<int>(std::round(screen_x));
        SDL_RenderDrawLine(renderer, sx, static_cast<int>(top), sx, static_cast<int>(bottom));
};

    auto draw_horizontal = [&](int y, Uint8 alpha) {
        float screen_y = center_px.y - (static_cast<float>(y) - center_world_.y) * scale;
        if (screen_y < top - 1.0f || screen_y > bottom + 1.0f) return;
        SDL_Color color = with_alpha(base, alpha);
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
        const int sy = static_cast<int>(std::round(screen_y));
        SDL_RenderDrawLine(renderer, static_cast<int>(left), sy, static_cast<int>(right), sy);
};

    for (int x = start_x; x <= end_x; ++x) {
        if (x == 0) continue;
        const bool major = (kMajorGridInterval > 0) && (x % kMajorGridInterval == 0);
        draw_vertical(x, major ? kMajorGridAlpha : kMinorGridAlpha);
    }
    for (int y = start_y; y <= end_y; ++y) {
        if (y == 0) continue;
        const bool major = (kMajorGridInterval > 0) && (y % kMajorGridInterval == 0);
        draw_horizontal(y, major ? kMajorGridAlpha : kMinorGridAlpha);
    }

    SDL_Color axis = DMStyles::AccentButton().press_bg;
    axis = with_alpha(axis, kAxisAlpha);
    float axis_x = center_px.x + (0.0f - center_world_.x) * scale;
    if (axis_x >= left - 1.0f && axis_x <= right + 1.0f) {
        int sx = static_cast<int>(std::round(axis_x));
        SDL_SetRenderDrawColor(renderer, axis.r, axis.g, axis.b, axis.a);
        SDL_RenderDrawLine(renderer, sx, static_cast<int>(top), sx, static_cast<int>(bottom));
    }
    float axis_y = center_px.y - (0.0f - center_world_.y) * scale;
    if (axis_y >= top - 1.0f && axis_y <= bottom + 1.0f) {
        int sy = static_cast<int>(std::round(axis_y));
        SDL_SetRenderDrawColor(renderer, axis.r, axis.g, axis.b, axis.a);
        SDL_RenderDrawLine(renderer, static_cast<int>(left), sy, static_cast<int>(right), sy);
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
                const float dx_units = (current.x - drag_last_mouse_.x) / scale;
                const float dy_units = -(current.y - drag_last_mouse_.y) / scale;
                drag_target_world_.x += dx_units;
                drag_target_world_.y += dy_units;
                drag_last_mouse_ = current;

                SDL_FPoint rounded_target = round_point_to_pixel(drag_target_world_);
                drag_target_world_ = rounded_target;

                std::vector<SDL_FPoint> fallback_positions;
                const std::vector<SDL_FPoint>* base_positions = nullptr;
                if (drag_base_positions_.size() == frames_.size()) {
                    base_positions = &drag_base_positions_;
                } else {
                    fallback_positions = positions_;
                    base_positions = &fallback_positions;
                }
                apply_frame_move_from_base(selected_index_, rounded_target, *base_positions);
                if (selected_index_ >= 0 && selected_index_ < static_cast<int>(positions_.size())) {
                    drag_target_world_ = positions_[selected_index_];
                }
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
                if (hovered_index_ >= 0 && hovered_index_ == selected_index_ && selected_index_ > 0) {

                    dragging_frame_ = true;
                    drag_last_mouse_ = SDL_Point{e.button.x, e.button.y};
                    drag_target_world_ = positions_[selected_index_];
                    drag_base_positions_ = positions_;
                } else if (selected_index_ > 0) {

                    std::vector<SDL_FPoint> base_positions = positions_;
                    SDL_FPoint world = screen_to_world(SDL_Point{e.button.x, e.button.y});

                    world = snap_to_resolution(world, snap_resolution_);
                    apply_frame_move_from_base(selected_index_, world, base_positions);
                    drag_target_world_ = world;
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
            if (e.button.button == SDL_BUTTON_LEFT) {
                if (dragging_frame_) {
                    dragging_frame_ = false;
                    drag_base_positions_.clear();
                }
                return true;
            }
            if ((e.button.button == SDL_BUTTON_RIGHT || e.button.button == SDL_BUTTON_MIDDLE) && panning_) {
                panning_ = false;
                return true;
            }
            break;
        }
        case SDL_MOUSEWHEEL: {
            int mx = 0, my = 0;
            SDL_GetMouseState(&mx, &my);
            last_mouse_.x = mx;
            last_mouse_.y = my;
            if (!within_bounds(mx, my)) {
                return false;
            }
            int wheel_y = e.wheel.y;
#if SDL_VERSION_ATLEAST(2,0,18)
            if (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                wheel_y = -wheel_y;
            }
#endif
            if (wheel_y != 0) {
                apply_zoom(static_cast<float>(wheel_y));
                return true;
            }
            return false;
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
        if (!std::isfinite(zoom_) || zoom_ <= 0.0f) {
            zoom_ = 16.0f;
        }
        zoom_ = std::clamp(zoom_, kMinZoom, kMaxZoom);
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

    if (!std::isfinite(min_x) || !std::isfinite(min_y)) {
        center_world_ = SDL_FPoint{0.0f, 0.0f};
        if (!std::isfinite(zoom_) || zoom_ <= 0.0f) {
            zoom_ = 16.0f;
        }
        zoom_ = std::clamp(zoom_, kMinZoom, kMaxZoom);
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
    const float fit_zoom = std::min(scale_x, scale_y);
    if (std::isfinite(fit_zoom) && fit_zoom > 0.0f) {
        zoom_ = std::clamp(fit_zoom, kMinZoom, kMaxZoom);
    } else {
        zoom_ = std::clamp(zoom_, kMinZoom, kMaxZoom);
    }
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

void MovementCanvas::set_animation_context(std::shared_ptr<PreviewProvider> provider,
                                           const std::string& animation_id,
                                           float scale_percentage) {
    preview_provider_ = std::move(provider);
    animation_id_ = animation_id;
    base_scale_percentage_ = std::isfinite(scale_percentage) && scale_percentage > 0.0f ? scale_percentage : 100.0f;

    pixels_per_unit_ = 1.0f;
}

void MovementCanvas::apply_frame_move_from_base(int index, const SDL_FPoint& new_position,
                                                const std::vector<SDL_FPoint>& base_positions) {
    if (index <= 0) return;
    if (static_cast<size_t>(index) >= frames_.size()) return;
    if (base_positions.size() != frames_.size()) return;

    frames_.front().dx = 0.0f;
    frames_.front().dy = 0.0f;

    if (!smoothing_enabled_) {
        SDL_FPoint prev_abs = base_positions[index - 1];
        frames_[index].dx = round_delta_to_pixel(new_position.x - prev_abs.x);
        frames_[index].dy = round_delta_to_pixel(new_position.y - prev_abs.y);

        SDL_FPoint last_abs = new_position;
        for (int j = index + 1; j < static_cast<int>(frames_.size()); ++j) {
            const SDL_FPoint desired = base_positions[j];
            frames_[j].dx = round_delta_to_pixel(desired.x - last_abs.x);
            frames_[j].dy = round_delta_to_pixel(desired.y - last_abs.y);
            last_abs = desired;
        }
        rebuild_path();
        return;
    }

    const int n = static_cast<int>(frames_.size());
    const int k = index;
    const SDL_FPoint start = base_positions.front();
    const SDL_FPoint end   = base_positions.back();

    if (!smoothing_curve_enabled_) {

        const int steps1 = k;
        const double seg1_dx = static_cast<double>(new_position.x - start.x);
        const double seg1_dy = static_cast<double>(new_position.y - start.y);
        int accum1_x = 0;
        int accum1_y = 0;
        for (int i = 1; i <= steps1; ++i) {
            const double t = steps1 > 0 ? static_cast<double>(i) / static_cast<double>(steps1) : 1.0;
            const double target_x = (i == steps1) ? seg1_dx : (seg1_dx * t);
            const double target_y = (i == steps1) ? seg1_dy : (seg1_dy * t);
            const int rounded_x = static_cast<int>(std::lround(target_x));
            const int rounded_y = static_cast<int>(std::lround(target_y));
            const int step_x = rounded_x - accum1_x;
            const int step_y = rounded_y - accum1_y;
            accum1_x = rounded_x;
            accum1_y = rounded_y;
            frames_[i].dx = round_delta_to_pixel(static_cast<float>(step_x));
            frames_[i].dy = round_delta_to_pixel(static_cast<float>(step_y));
        }

        const int steps2 = std::max(0, (n - 1) - k);
        const double seg2_dx = static_cast<double>(end.x - new_position.x);
        const double seg2_dy = static_cast<double>(end.y - new_position.y);
        int accum2_x = 0;
        int accum2_y = 0;
        for (int s = 1; s <= steps2; ++s) {
            const double u = steps2 > 0 ? static_cast<double>(s) / static_cast<double>(steps2) : 1.0;
            const double target_x = (s == steps2) ? seg2_dx : (seg2_dx * u);
            const double target_y = (s == steps2) ? seg2_dy : (seg2_dy * u);
            const int rounded_x = static_cast<int>(std::lround(target_x));
            const int rounded_y = static_cast<int>(std::lround(target_y));
            const int step_x = rounded_x - accum2_x;
            const int step_y = rounded_y - accum2_y;
            accum2_x = rounded_x;
            accum2_y = rounded_y;
            const int j = k + s;
            frames_[j].dx = round_delta_to_pixel(static_cast<float>(step_x));
            frames_[j].dy = round_delta_to_pixel(static_cast<float>(step_y));
        }
    } else {

        const int steps1 = k;
        const int steps2 = std::max(0, (n - 1) - k);

        SDL_FPoint ctrl1 = start;
        if (steps1 > 1) {
            int mid1 = std::clamp(k / 2, 0, k);
            ctrl1 = base_positions[static_cast<size_t>(mid1)];
        }
        SDL_FPoint ctrl2 = end;
        if (steps2 > 1) {
            int mid2 = k + std::max(1, (n - 1 - k) / 2);
            ctrl2 = base_positions[static_cast<size_t>(std::clamp(mid2, 0, n - 1))];
        }

        if (steps1 > 0) {
            auto poly1 = bezier2_sampled_polyline(start, ctrl1, new_position, std::max(32, steps1 * 8));
            auto cum1  = cumulative_lengths(poly1);
            const double total1 = cum1.back();
            int accum1_x = 0;
            int accum1_y = 0;
            for (int i = 1; i <= steps1; ++i) {
                const double target_len = (total1 * static_cast<double>(i)) / static_cast<double>(steps1);
                SDL_FPoint abs = (i == steps1) ? new_position : interpolate_along_polyline(poly1, cum1, target_len);
                const double rel_x = static_cast<double>(abs.x - start.x);
                const double rel_y = static_cast<double>(abs.y - start.y);
                const int rounded_x = static_cast<int>(std::lround(rel_x));
                const int rounded_y = static_cast<int>(std::lround(rel_y));
                const int step_x = rounded_x - accum1_x;
                const int step_y = rounded_y - accum1_y;
                accum1_x = rounded_x;
                accum1_y = rounded_y;
                frames_[i].dx = round_delta_to_pixel(static_cast<float>(step_x));
                frames_[i].dy = round_delta_to_pixel(static_cast<float>(step_y));
            }
        }

        if (steps2 > 0) {
            auto poly2 = bezier2_sampled_polyline(new_position, ctrl2, end, std::max(32, steps2 * 8));
            auto cum2  = cumulative_lengths(poly2);
            const double total2 = cum2.back();
            int accum2_x = 0;
            int accum2_y = 0;
            for (int s = 1; s <= steps2; ++s) {
                const double target_len = (total2 * static_cast<double>(s)) / static_cast<double>(steps2);
                SDL_FPoint abs = (s == steps2) ? end : interpolate_along_polyline(poly2, cum2, target_len);
                const double rel_x = static_cast<double>(abs.x - new_position.x);
                const double rel_y = static_cast<double>(abs.y - new_position.y);
                const int rounded_x = static_cast<int>(std::lround(rel_x));
                const int rounded_y = static_cast<int>(std::lround(rel_y));
                const int step_x = rounded_x - accum2_x;
                const int step_y = rounded_y - accum2_y;
                accum2_x = rounded_x;
                accum2_y = rounded_y;
                const int j = k + s;
                frames_[j].dx = round_delta_to_pixel(static_cast<float>(step_x));
                frames_[j].dy = round_delta_to_pixel(static_cast<float>(step_y));
            }
        }
    }

    rebuild_path();
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

float MovementCanvas::screen_pixels_per_unit() const {
    const float scale = pixels_per_unit_ * zoom_;
    if (!std::isfinite(scale) || scale <= 0.0f) {
        return 1.0f;
    }
    return scale;
}

float MovementCanvas::document_scale_factor() const {
    if (!std::isfinite(base_scale_percentage_) || base_scale_percentage_ <= 0.0f) {
        return 1.0f;
    }
    return base_scale_percentage_ / 100.0f;
}

SDL_FPoint MovementCanvas::frame_position_world(int frame_index) const {
    if (positions_.empty()) {
        return SDL_FPoint{0.0f, 0.0f};
    }
    int idx = std::clamp(frame_index, 0, static_cast<int>(positions_.size()) - 1);
    return positions_[static_cast<size_t>(idx)];
}

SDL_FPoint MovementCanvas::frame_anchor_world(int frame_index) const {
    if (!anchor_follows_movement_) {
        return SDL_FPoint{0.0f, 0.0f};
    }
    return frame_position_world(frame_index);
}

SDL_FPoint MovementCanvas::frame_anchor_screen(int frame_index) const {
    return world_to_screen(frame_anchor_world(frame_index));
}

}
