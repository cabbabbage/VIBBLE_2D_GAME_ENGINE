#include "map_editor.hpp"

#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "dev_mode/dm_styles.hpp"
#include "dev_mode/draw_utils.hpp"
#include "dev_mode/dev_mode_utils.hpp"
#include "dev_mode_color_utils.hpp"
#include "room_overlay_renderer.hpp"
#include "render/warped_screen_grid.hpp"
#include "map_generation/room.hpp"
#include "utils/area.hpp"
#include "utils/input.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <tuple>
#include <vector>

namespace {
constexpr int kBoundsPadding = 256;

}

MapEditor::MapEditor(Assets* owner)
    : assets_(owner) {}

MapEditor::~MapEditor() {
    release_font();
}

void MapEditor::set_input(Input* input) {
    input_ = input;
}

void MapEditor::set_rooms(std::vector<Room*>* rooms) {
    rooms_ = rooms;
    compute_bounds();
}

void MapEditor::set_screen_dimensions(int width, int height) {
    screen_w_ = width;
    screen_h_ = height;
}

void MapEditor::set_ui_blocker(std::function<bool(int, int)> blocker) {
    ui_blocker_ = std::move(blocker);
}

void MapEditor::set_label_safe_area_provider(std::function<SDL_Rect()> provider) {
    label_safe_area_provider_ = std::move(provider);
}

void MapEditor::set_camera_override_for_testing(WarpedScreenGrid* camera_override) {
    camera_override_for_testing_ = camera_override;
}

void MapEditor::set_enabled(bool enabled) {
    if (enabled == enabled_) return;
    if (enabled) {
        enter();
    } else {
        exit(false);
    }
}

void MapEditor::enter() {
    if (enabled_) return;
    enabled_ = true;
    pending_selection_ = nullptr;
    has_entry_center_ = false;

    if (WarpedScreenGrid* cam = active_camera()) {
        prev_manual_override_ = cam->is_manual_zoom_override();
        prev_focus_override_ = cam->has_focus_override();
        if (prev_focus_override_) {
            prev_focus_point_ = cam->get_focus_override_point();
        } else {
            prev_focus_point_ = SDL_Point{0, 0};
        }
        entry_center_ = cam->get_screen_center();
        has_entry_center_ = true;
        cam->set_manual_zoom_override(true);
    }

    compute_bounds();
}

void MapEditor::exit(bool focus_player, bool restore_previous_state) {
    has_entry_center_ = false;
    if (!enabled_) {
        restore_camera_state(focus_player, restore_previous_state);
        return;
    }
    enabled_ = false;
    restore_camera_state(focus_player, restore_previous_state);
    pending_selection_ = nullptr;
}

void MapEditor::update(const Input& input) {
    if (!enabled_) return;
    WarpedScreenGrid* cam = active_camera();
    if (!cam) return;

    SDL_Point screen_pt{input.getX(), input.getY()};
    SDL_FPoint map_pt_f = cam->screen_to_map(screen_pt);
    SDL_Point map_pt{static_cast<int>(std::lround(map_pt_f.x)), static_cast<int>(std::lround(map_pt_f.y))};
    const bool pointer_over_ui = ui_blocker_ ? ui_blocker_(screen_pt.x, screen_pt.y) : false;

    const bool shift_down =
        input.isScancodeDown(SDL_SCANCODE_LSHIFT) || input.isScancodeDown(SDL_SCANCODE_RSHIFT);

    Room* area_hit = hit_test_room(map_pt);
    Room* label_hit = nullptr;

    if (shift_down) {
        for (const auto& entry : label_rects_) {
            if (SDL_PointInRect(&screen_pt, &entry.second)) {
                label_hit = entry.first;
                break;
            }
        }
    }

    Room* hit = label_hit ? label_hit : area_hit;

    const bool left_down = input.isDown(Input::LEFT);
    const bool left_pressed = input.wasPressed(Input::LEFT);
    const bool pan_blocked = pointer_over_ui || (shift_down && hit != nullptr && (left_down || left_pressed));
    pan_zoom_.handle_input(*cam, input, pan_blocked);

    if (pointer_over_ui) {
        return;
    }

    if (input.wasClicked(Input::LEFT)) {
        if (shift_down && hit) {
            pending_selection_ = hit;
            if (input_) {
                input_->consumeMouseButton(Input::LEFT);
            }
        }
    }
}

void MapEditor::render(SDL_Renderer* renderer) {
    if (!enabled_) return;
    if (!renderer) return;
    if (!rooms_ || rooms_->empty()) return;

    ensure_font();
    if (!label_font_) return;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    label_rects_.clear();

    active_label_bounds_ = effective_label_bounds();

    struct LabelInfo {
        Room* room = nullptr;
        SDL_FPoint desired_center{0.0f, 0.0f};
        float priority = 0.0f;
};

    std::vector<LabelInfo> render_queue;
    render_queue.reserve(rooms_->size());

    const float bounds_center_x = static_cast<float>(active_label_bounds_.x) + static_cast<float>(active_label_bounds_.w) * 0.5f;
    const float bounds_center_y = static_cast<float>(active_label_bounds_.y) + static_cast<float>(active_label_bounds_.h) * 0.5f;
    SDL_FPoint screen_center{bounds_center_x, bounds_center_y};

    WarpedScreenGrid& view = assets_->getView();

    for (Room* room : *rooms_) {
        if (!room || !room->room_area) continue;
        const auto style = dm_draw::ResolveRoomBoundsOverlayStyle(room->display_color());

        dm_draw::RenderRoomBoundsOverlay( renderer, view, *room->room_area, style);

        SDL_Point center = room->room_area->get_center();
        SDL_FPoint screen_pt = view.map_to_screen(center);
        SDL_FPoint desired_center{screen_pt.x,
                                  screen_pt.y - kLabelVerticalOffset};

        float dx = desired_center.x - screen_center.x;
        float dy = desired_center.y - screen_center.y;
        float dist2 = dx * dx + dy * dy;

        render_queue.push_back(LabelInfo{room, desired_center, dist2});
    }

    std::sort(render_queue.begin(), render_queue.end(), [](const LabelInfo& a, const LabelInfo& b) {
        if (a.priority == b.priority) {
            return a.room < b.room;
        }
        return a.priority < b.priority;
    });

    for (const auto& info : render_queue) {
        if (!info.room) continue;
        render_room_label(renderer, info.room, info.desired_center);
    }
}

Room* MapEditor::consume_selected_room() {
    Room* out = pending_selection_;
    pending_selection_ = nullptr;
    return out;
}

void MapEditor::focus_on_room(Room* room) {
    if (!room || !room->room_area) return;
    WarpedScreenGrid* cam = active_camera();
    if (!cam) return;

    Area adjusted = cam->convert_area_to_aspect(*room->room_area);
    cam->set_manual_zoom_override(true);
    cam->set_focus_override(adjusted.get_center());
    cam->zoom_to_area(adjusted, 0);
}

void MapEditor::ensure_font() {
    if (label_font_) return;
    label_font_ = TTF_OpenFont(dm::FONT_PATH, 18);
}

void MapEditor::release_font() {
    if (label_font_) {
        TTF_CloseFont(label_font_);
        label_font_ = nullptr;
    }
}

bool MapEditor::compute_bounds() {
    if (!rooms_) {
        has_bounds_ = false;
        return false;
    }

    bool first = true;
    Bounds b{};
    for (Room* room : *rooms_) {
        if (!room || !room->room_area) continue;
        auto [minx, miny, maxx, maxy] = room->room_area->get_bounds();
        if (first) {
            b.min_x = minx;
            b.min_y = miny;
            b.max_x = maxx;
            b.max_y = maxy;
            first = false;
        } else {
            b.min_x = std::min(b.min_x, minx);
            b.min_y = std::min(b.min_y, miny);
            b.max_x = std::max(b.max_x, maxx);
            b.max_y = std::max(b.max_y, maxy);
        }
    }

    if (first) {
        has_bounds_ = false;
        return false;
    }

    bounds_ = b;
    has_bounds_ = true;
    return true;
}

void MapEditor::apply_camera_to_bounds() {
    WarpedScreenGrid* cam = active_camera();
    if (!cam) return;
    cam->set_manual_zoom_override(true);

    Room* spawn_room = find_spawn_room();
    SDL_Point spawn_center{0, 0};
    bool has_spawn_center = false;
    if (spawn_room && spawn_room->room_area) {
        spawn_center = spawn_room->room_area->get_center();
        has_spawn_center = true;
    }

    if (has_bounds_) {
        int min_x = bounds_.min_x - kBoundsPadding;
        int min_y = bounds_.min_y - kBoundsPadding;
        int max_x = bounds_.max_x + kBoundsPadding;
        int max_y = bounds_.max_y + kBoundsPadding;

        auto distance = [](int a, int b) { return (a > b) ? (a - b) : (b - a); };
        SDL_Point bounds_center{ (min_x + max_x) / 2, (min_y + max_y) / 2 };
        SDL_Point center = has_entry_center_ ? entry_center_
                                             : (has_spawn_center ? spawn_center : bounds_center);
        int half_w = std::max({ distance(center.x, min_x), distance(center.x, max_x), 1 });
        int half_h = std::max({ distance(center.y, min_y), distance(center.y, max_y), 1 });
        int left = center.x - half_w;
        int right = center.x + half_w;
        int top = center.y - half_h;
        int bottom = center.y + half_h;

        std::vector<SDL_Point> pts{
            {left, top},
            {right, top},
            {right, bottom},
            {left, bottom},
};
        Area area("map_bounds", pts, 3);
        cam->set_focus_override(center);
        cam->zoom_to_area(area, 0);
    } else if (has_entry_center_) {
        cam->set_focus_override(entry_center_);
        cam->zoom_to_scale(1.0, 0);
    } else if (has_spawn_center) {
        cam->set_focus_override(spawn_center);
        if (spawn_room && spawn_room->room_area) {
            Area adjusted = cam->convert_area_to_aspect(*spawn_room->room_area);
            cam->zoom_to_area(adjusted, 0);
        } else {
            cam->zoom_to_scale(1.0, 0);
        }
    } else {
        cam->set_focus_override(SDL_Point{0, 0});
        cam->zoom_to_scale(1.0, 0);
    }
}

Room* MapEditor::find_spawn_room() const {
    if (!rooms_) return nullptr;
    for (Room* room : *rooms_) {
        if (room && room->is_spawn_room()) {
            return room;
        }
    }
    return nullptr;
}

void MapEditor::restore_camera_state(bool focus_player, bool restore_previous_state) {
    WarpedScreenGrid* cam = active_camera();
    if (!cam) return;

    if (focus_player) {
        cam->clear_focus_override();
        cam->set_manual_zoom_override(false);
        return;
    }

    if (!restore_previous_state) {
        return;
    }

    cam->set_manual_zoom_override(prev_manual_override_);
    if (prev_focus_override_) {
        cam->set_focus_override(prev_focus_point_);
    } else {
        cam->clear_focus_override();
    }
}

WarpedScreenGrid* MapEditor::active_camera() const {
    if (camera_override_for_testing_) {
        return camera_override_for_testing_;
    }
    if (!assets_) {
        return nullptr;
    }
    return &assets_->getView();
}

Room* MapEditor::hit_test_room(SDL_Point map_point) const {
    if (!rooms_) return nullptr;
    for (Room* room : *rooms_) {
        if (!room || !room->room_area) continue;
        if (room->room_area->contains_point(map_point)) {
            return room;
        }
    }
    return nullptr;
}

void MapEditor::render_room_label(SDL_Renderer* renderer, Room* room, SDL_FPoint desired_center) {
    if (!room || !room->room_area || !assets_) return;
    if (!label_font_) return;

    const std::string& name = room->room_name.empty() ? std::string("<unnamed>") : room->room_name;
    SDL_Color base_color = room->display_color();
    SDL_Color text_color = display_color_luminance(base_color) > 0.55f
                               ? SDL_Color{20, 20, 20, 255}
                               : kLabelText;

    SDL_Surface* text_surface = TTF_RenderUTF8_Blended(label_font_, name.c_str(), text_color);
    if (!text_surface) return;

    SDL_Rect bg_rect = label_background_rect(text_surface, desired_center);
    bg_rect = resolve_edge_overlap(bg_rect, desired_center);

    label_rects_.emplace_back(room, bg_rect);

    SDL_Color bg_color = devmode::utils::with_alpha(lighten(base_color, 0.08f), 205);
    SDL_Color border_color = devmode::utils::with_alpha(darken(base_color, 0.3f), 235);

    const int radius = std::min(DMStyles::CornerRadius(), std::min(bg_rect.w, bg_rect.h) / 2);
    const int bevel = std::min(DMStyles::BevelDepth(), std::max(0, std::min(bg_rect.w, bg_rect.h) / 2));
    dm_draw::DrawBeveledRect( renderer, bg_rect, radius, bevel, bg_color, bg_color, bg_color, false, 0.0f, 0.0f);
    dm_draw::DrawRoundedOutline( renderer, bg_rect, radius, 1, border_color);

    SDL_Texture* text_tex = SDL_CreateTextureFromSurface(renderer, text_surface);
    if (text_tex) {
        SDL_Rect dst{bg_rect.x + kLabelPadding, bg_rect.y + kLabelPadding, text_surface->w, text_surface->h};
        SDL_RenderCopy(renderer, text_tex, nullptr, &dst);
        SDL_DestroyTexture(text_tex);
    }
    SDL_FreeSurface(text_surface);
}

SDL_Rect MapEditor::label_background_rect(const SDL_Surface* surface, SDL_FPoint desired_center) const {
    int text_w = surface ? surface->w : 0;
    int text_h = surface ? surface->h : 0;
    int rect_w = text_w + kLabelPadding * 2;
    int rect_h = text_h + kLabelPadding * 2;

    SDL_Rect rect{};
    rect.w = rect_w;
    rect.h = rect_h;

    if (screen_w_ <= 0 || screen_h_ <= 0) {
        rect.x = static_cast<int>(std::lround(desired_center.x - static_cast<float>(rect_w) * 0.5f));
        rect.y = static_cast<int>(std::lround(desired_center.y - static_cast<float>(rect_h) * 0.5f));
        return rect;
    }

    const SDL_Rect bounds = active_label_bounds_.w > 0 && active_label_bounds_.h > 0
                                ? active_label_bounds_
                                : SDL_Rect{0, 0, std::max(0, screen_w_), std::max(0, screen_h_)};

    const float half_w = static_cast<float>(rect_w) * 0.5f;
    const float half_h = static_cast<float>(rect_h) * 0.5f;
    const float min_x = static_cast<float>(bounds.x) + half_w;
    const float max_x = static_cast<float>(bounds.x + bounds.w) - half_w;
    const float min_y = static_cast<float>(bounds.y) + half_h;
    const float max_y = static_cast<float>(bounds.y + bounds.h) - half_h;

    auto clamp_center = [&](const SDL_FPoint& point) {
        SDL_FPoint clamped = point;
        clamped.x = std::clamp(clamped.x, min_x, max_x);
        clamped.y = std::clamp(clamped.y, min_y, max_y);
        return clamped;
};

    SDL_FPoint center = clamp_center(desired_center);

    const bool inside = desired_center.x >= min_x && desired_center.x <= max_x &&
                        desired_center.y >= min_y && desired_center.y <= max_y;

    if (!inside) {
        SDL_FPoint screen_center{ static_cast<float>(bounds.x) + static_cast<float>(bounds.w) * 0.5f,
                                  static_cast<float>(bounds.y) + static_cast<float>(bounds.h) * 0.5f };
        const float dx = desired_center.x - screen_center.x;
        const float dy = desired_center.y - screen_center.y;
        const float epsilon = 0.0001f;

        if (std::fabs(dx) > epsilon || std::fabs(dy) > epsilon) {
            float t_min = 1.0f;

            auto update_t = [&](float boundary, float origin, float delta) {
                if (std::fabs(delta) < epsilon) return;
                float t = (boundary - origin) / delta;
                if (t >= 0.0f) {
                    t_min = std::min(t_min, t);
                }
};

            if (dx > 0.0f) update_t(max_x, screen_center.x, dx);
            else if (dx < 0.0f) update_t(min_x, screen_center.x, dx);

            if (dy > 0.0f) update_t(max_y, screen_center.y, dy);
            else if (dy < 0.0f) update_t(min_y, screen_center.y, dy);

            center.x = screen_center.x + dx * t_min;
            center.y = screen_center.y + dy * t_min;
            center = clamp_center(center);
        }
    }

    rect.x = static_cast<int>(std::lround(center.x - half_w));
    rect.y = static_cast<int>(std::lround(center.y - half_h));
    return rect;
}

SDL_Rect MapEditor::resolve_edge_overlap(SDL_Rect rect, SDL_FPoint desired_center) {
    if (screen_w_ <= 0 || screen_h_ <= 0) {
        return rect;
    }

    const SDL_Rect bounds = active_label_bounds_.w > 0 && active_label_bounds_.h > 0
                                ? active_label_bounds_
                                : SDL_Rect{0, 0, std::max(0, screen_w_), std::max(0, screen_h_)};

    const int tolerance = 1;
    const bool touches_left   = rect.x <= bounds.x + tolerance;
    const bool touches_right  = rect.x + rect.w >= (bounds.x + bounds.w) - tolerance;
    const bool touches_top    = rect.y <= bounds.y + tolerance;
    const bool touches_bottom = rect.y + rect.h >= (bounds.y + bounds.h) - tolerance;

    if (touches_top || touches_bottom) {
        rect = resolve_horizontal_edge_overlap(rect, desired_center.x, touches_top);
    }

    if (touches_left || touches_right) {
        rect = resolve_vertical_edge_overlap(rect, desired_center.y, touches_left);
    }

    return rect;
}

SDL_Rect MapEditor::resolve_horizontal_edge_overlap(SDL_Rect rect, float desired_center_x, bool top_edge) {
    if (screen_w_ <= 0) return rect;

    const SDL_Rect bounds = active_label_bounds_.w > 0 && active_label_bounds_.h > 0
                                ? active_label_bounds_
                                : SDL_Rect{0, 0, std::max(0, screen_w_), std::max(0, screen_h_)};
    const int min_x = bounds.x;
    const int max_x = std::max(bounds.x, bounds.x + std::max(0, bounds.w - rect.w));
    if (max_x <= min_x) {
        rect.x = min_x;
        return rect;
    }

    std::vector<SDL_Rect> same_edge_rects;
    same_edge_rects.reserve(label_rects_.size());
    const int tolerance = 1;

    for (const auto& entry : label_rects_) {
        const SDL_Rect& other = entry.second;
        bool other_on_edge = top_edge ? other.y <= bounds.y + tolerance
                                      : other.y + other.h >= (bounds.y + bounds.h) - tolerance;
        if (other_on_edge) {
            same_edge_rects.push_back(other);
        }
    }

    if (same_edge_rects.empty()) {
        rect.x = std::clamp(static_cast<int>(std::lround(desired_center_x - rect.w * 0.5f)), min_x, max_x);
        return rect;
    }

    std::vector<int> to_process;
    to_process.reserve(same_edge_rects.size() * 2 + 3);

    int target_x = std::clamp(static_cast<int>(std::lround(desired_center_x - rect.w * 0.5f)), min_x, max_x);
    to_process.push_back(target_x);
    to_process.push_back(min_x);
    to_process.push_back(max_x);

    std::vector<int> visited;
    visited.reserve(to_process.size());

    float best_penalty = std::numeric_limits<float>::max();
    int best_x = target_x;
    bool found_position = false;

    while (!to_process.empty()) {
        int candidate_x = to_process.back();
        to_process.pop_back();

        if (std::find(visited.begin(), visited.end(), candidate_x) != visited.end()) {
            continue;
        }
        visited.push_back(candidate_x);

        SDL_Rect candidate = rect;
        candidate.x = candidate_x;

        std::vector<SDL_Rect> overlapping;
        for (const SDL_Rect& other : same_edge_rects) {
            if (rects_overlap(candidate, other)) {
                overlapping.push_back(other);
            }
        }

        if (overlapping.empty()) {
            float center_x = static_cast<float>(candidate.x) + static_cast<float>(candidate.w) * 0.5f;
            float penalty = std::fabs(center_x - desired_center_x);
            if (penalty < best_penalty - 0.01f || (!found_position && penalty <= best_penalty + 0.01f)) {
                best_penalty = penalty;
                best_x = candidate_x;
                found_position = true;
                if (penalty <= 0.01f) {
                    break;
                }
            }
            continue;
        }

        for (const SDL_Rect& other : overlapping) {
            int left = std::clamp(other.x - rect.w, min_x, max_x);
            int right = std::clamp(other.x + other.w, min_x, max_x);

            if (std::find(visited.begin(), visited.end(), left) == visited.end()) {
                to_process.push_back(left);
            }
            if (std::find(visited.begin(), visited.end(), right) == visited.end()) {
                to_process.push_back(right);
            }
        }
    }

    rect.x = found_position ? best_x : target_x;
    return rect;
}

SDL_Rect MapEditor::resolve_vertical_edge_overlap(SDL_Rect rect, float desired_center_y, bool left_edge) {
    if (screen_h_ <= 0) return rect;

    const SDL_Rect bounds = active_label_bounds_.w > 0 && active_label_bounds_.h > 0
                                ? active_label_bounds_
                                : SDL_Rect{0, 0, std::max(0, screen_w_), std::max(0, screen_h_)};
    const int min_y = bounds.y;
    const int max_y = std::max(bounds.y, bounds.y + std::max(0, bounds.h - rect.h));
    if (max_y <= min_y) {
        rect.y = min_y;
        return rect;
    }

    std::vector<SDL_Rect> same_edge_rects;
    same_edge_rects.reserve(label_rects_.size());
    const int tolerance = 1;

    for (const auto& entry : label_rects_) {
        const SDL_Rect& other = entry.second;
        bool other_on_edge = left_edge ? other.x <= bounds.x + tolerance
                                       : other.x + other.w >= (bounds.x + bounds.w) - tolerance;
        if (other_on_edge) {
            same_edge_rects.push_back(other);
        }
    }

    if (same_edge_rects.empty()) {
        rect.y = std::clamp(static_cast<int>(std::lround(desired_center_y - rect.h * 0.5f)), min_y, max_y);
        return rect;
    }

    std::vector<int> to_process;
    to_process.reserve(same_edge_rects.size() * 2 + 3);

    int target_y = std::clamp(static_cast<int>(std::lround(desired_center_y - rect.h * 0.5f)), min_y, max_y);
    to_process.push_back(target_y);
    to_process.push_back(min_y);
    to_process.push_back(max_y);

    std::vector<int> visited;
    visited.reserve(to_process.size());

    float best_penalty = std::numeric_limits<float>::max();
    int best_y = target_y;
    bool found_position = false;

    while (!to_process.empty()) {
        int candidate_y = to_process.back();
        to_process.pop_back();

        if (std::find(visited.begin(), visited.end(), candidate_y) != visited.end()) {
            continue;
        }
        visited.push_back(candidate_y);

        SDL_Rect candidate = rect;
        candidate.y = candidate_y;

        std::vector<SDL_Rect> overlapping;
        for (const SDL_Rect& other : same_edge_rects) {
            if (rects_overlap(candidate, other)) {
                overlapping.push_back(other);
            }
        }

        if (overlapping.empty()) {
            float center_y = static_cast<float>(candidate.y) + static_cast<float>(candidate.h) * 0.5f;
            float penalty = std::fabs(center_y - desired_center_y);
            if (penalty < best_penalty - 0.01f || (!found_position && penalty <= best_penalty + 0.01f)) {
                best_penalty = penalty;
                best_y = candidate_y;
                found_position = true;
                if (penalty <= 0.01f) {
                    break;
                }
            }
            continue;
        }

        for (const SDL_Rect& other : overlapping) {
            int up = std::clamp(other.y - rect.h, min_y, max_y);
            int down = std::clamp(other.y + other.h, min_y, max_y);

            if (std::find(visited.begin(), visited.end(), up) == visited.end()) {
                to_process.push_back(up);
            }
            if (std::find(visited.begin(), visited.end(), down) == visited.end()) {
                to_process.push_back(down);
            }
        }
    }

    rect.y = found_position ? best_y : target_y;
    return rect;
}

bool MapEditor::rects_overlap(const SDL_Rect& a, const SDL_Rect& b) {
    return !(a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y || b.y + b.h <= a.y);
}

SDL_Rect MapEditor::effective_label_bounds() const {

    SDL_Rect fallback{0, 0, std::max(0, screen_w_), std::max(0, screen_h_)};
    if (!label_safe_area_provider_) {
        return fallback;
    }
    SDL_Rect area = label_safe_area_provider_();

    if (area.w <= 0 || area.h <= 0) return fallback;
    if (screen_w_ > 0 && screen_h_ > 0) {

        int max_x = std::max(0, screen_w_ - area.w);
        int max_y = std::max(0, screen_h_ - area.h);
        area.x = std::clamp(area.x, 0, max_x);
        area.y = std::clamp(area.y, 0, max_y);

        if (area.x + area.w > screen_w_) area.w = std::max(0, screen_w_ - area.x);
        if (area.y + area.h > screen_h_) area.h = std::max(0, screen_h_ - area.y);
    }
    return area;
}
