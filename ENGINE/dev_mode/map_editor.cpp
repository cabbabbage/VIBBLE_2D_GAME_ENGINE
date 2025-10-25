#include "map_editor.hpp"

#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "dev_mode/dm_styles.hpp"
#include "dev_mode/draw_utils.hpp"
#include "render/camera.hpp"
#include "map_generation/room.hpp"
#include "utils/area.hpp"
#include "utils/input.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <tuple>
#include <vector>

namespace {
constexpr int kBoundsPadding = 256;
constexpr int kLabelPadding = 6;
constexpr int kLabelVerticalOffset = 32;
const SDL_Color kLabelBg{0, 0, 0, 180};
const SDL_Color kLabelBorder{255, 255, 255, 80};
const SDL_Color kLabelText{240, 240, 240, 255};
const SDL_Color kTrailLabelBg{10, 70, 30, 200};
const SDL_Color kTrailLabelBorder{60, 190, 110, 200};
const SDL_Color kTrailLabelText{210, 255, 220, 255};
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

void MapEditor::set_camera_override_for_testing(camera* camera_override) {
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

    if (camera* cam = active_camera()) {
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
    camera* cam = active_camera();
    if (!cam) return;

    SDL_Point screen_pt{input.getX(), input.getY()};
    SDL_Point map_pt = cam->screen_to_map(screen_pt);
    const bool pointer_over_ui = ui_blocker_ ? ui_blocker_(screen_pt.x, screen_pt.y) : false;

    Room* area_hit = hit_test_room(map_pt);
    Room* label_hit = nullptr;
    for (const auto& entry : label_rects_) {
        if (SDL_PointInRect(&screen_pt, &entry.second)) {
            label_hit = entry.first;
            break;
        }
    }

    Room* hit = label_hit ? label_hit : area_hit;

    pan_zoom_.handle_input(*cam, input, pointer_over_ui || hit != nullptr);

    if (pointer_over_ui) {
        return;
    }

    if (input.wasClicked(Input::LEFT)) {
        if (hit) {
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

    for (Room* room : *rooms_) {
        if (!room || !room->room_area) continue;
        render_room_label(renderer, room);
    }
}

Room* MapEditor::consume_selected_room() {
    Room* out = pending_selection_;
    pending_selection_ = nullptr;
    return out;
}

void MapEditor::focus_on_room(Room* room) {
    if (!room || !room->room_area) return;
    camera* cam = active_camera();
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
    camera* cam = active_camera();
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
        Area area("map_bounds", pts);
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
    camera* cam = active_camera();
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

camera* MapEditor::active_camera() const {
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

void MapEditor::render_room_label(SDL_Renderer* renderer, Room* room) {
    if (!room || !room->room_area || !assets_) return;
    if (!label_font_) return;

    const std::string& name = room->room_name.empty() ? std::string("<unnamed>") : room->room_name;
    bool is_trail = false;
    if (!room->type.empty()) {
        std::string lowered = room->type;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        is_trail = (lowered == "trail");
    }

    const SDL_Color& text_color = is_trail ? kTrailLabelText : kLabelText;
    SDL_Surface* text_surface = TTF_RenderUTF8_Blended(label_font_, name.c_str(), text_color);
    if (!text_surface) return;

    SDL_Point center = room->room_area->get_center();
    SDL_Point screen_pt = assets_->getView().map_to_screen(center);
    SDL_Rect bg_rect = label_background_rect(text_surface, screen_pt);

    label_rects_.emplace_back(room, bg_rect);

    const SDL_Color& bg_color = is_trail ? kTrailLabelBg : kLabelBg;
    const SDL_Color& border_color = is_trail ? kTrailLabelBorder : kLabelBorder;

    const int radius = std::min(DMStyles::CornerRadius(), std::min(bg_rect.w, bg_rect.h) / 2);
    const int bevel = std::min(DMStyles::BevelDepth(), std::max(0, std::min(bg_rect.w, bg_rect.h) / 2));
    dm_draw::DrawBeveledRect(
        renderer,
        bg_rect,
        radius,
        bevel,
        bg_color,
        bg_color,
        bg_color,
        false,
        0.0f,
        0.0f);
    dm_draw::DrawRoundedOutline(
        renderer,
        bg_rect,
        radius,
        1,
        border_color);

    SDL_Texture* text_tex = SDL_CreateTextureFromSurface(renderer, text_surface);
    if (text_tex) {
        SDL_Rect dst{bg_rect.x + kLabelPadding, bg_rect.y + kLabelPadding, text_surface->w, text_surface->h};
        SDL_RenderCopy(renderer, text_tex, nullptr, &dst);
        SDL_DestroyTexture(text_tex);
    }
    SDL_FreeSurface(text_surface);
}

SDL_Rect MapEditor::label_background_rect(const SDL_Surface* surface, SDL_Point screen_pos) const {
    int text_w = surface ? surface->w : 0;
    int text_h = surface ? surface->h : 0;
    int rect_w = text_w + kLabelPadding * 2;
    int rect_h = text_h + kLabelPadding * 2;

    SDL_Rect rect{};
    rect.w = rect_w;
    rect.h = rect_h;

    if (screen_w_ <= 0 || screen_h_ <= 0) {
        rect.x = screen_pos.x - rect_w / 2;
        rect.y = screen_pos.y - rect_h / 2 - kLabelVerticalOffset;
        return rect;
    }

    const float half_w = static_cast<float>(rect_w) * 0.5f;
    const float half_h = static_cast<float>(rect_h) * 0.5f;
    const float min_x = half_w;
    const float max_x = static_cast<float>(screen_w_) - half_w;
    const float min_y = half_h;
    const float max_y = static_cast<float>(screen_h_) - half_h;

    SDL_FPoint desired_center{static_cast<float>(screen_pos.x),
                              static_cast<float>(screen_pos.y - kLabelVerticalOffset)};

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
        SDL_FPoint screen_center{static_cast<float>(screen_w_) * 0.5f,
                                 static_cast<float>(screen_h_) * 0.5f};
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
