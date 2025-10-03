#include "map_editor.hpp"

#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "dev_mode/dm_styles.hpp"
#include "render/camera.hpp"
#include "room/room.hpp"
#include "utils/area.hpp"
#include "utils/input.hpp"

#include <algorithm>
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

    if (assets_) {
        camera& cam = assets_->getView();
        prev_manual_override_ = cam.is_manual_zoom_override();
        prev_focus_override_ = cam.has_focus_override();
        if (prev_focus_override_) {
            prev_focus_point_ = cam.get_focus_override_point();
        } else {
            prev_focus_point_ = SDL_Point{0, 0};
        }
        entry_center_ = cam.get_screen_center();
        has_entry_center_ = true;
    }

    compute_bounds();
    apply_camera_to_bounds();
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
    if (!assets_) return;

    camera& cam = assets_->getView();

    SDL_Point screen_pt{input.getX(), input.getY()};
    SDL_Point map_pt = cam.screen_to_map(screen_pt);
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

    pan_zoom_.handle_input(cam, input, pointer_over_ui || hit != nullptr);

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
    if (!room || !room->room_area || !assets_) return;

    camera& cam = assets_->getView();
    Area adjusted = cam.convert_area_to_aspect(*room->room_area);
    cam.set_manual_zoom_override(true);
    cam.set_focus_override(adjusted.get_center());
    cam.zoom_to_area(adjusted, 20);
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
    if (!assets_) return;
    camera& cam = assets_->getView();
    cam.set_manual_zoom_override(true);

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
        cam.set_focus_override(center);
        cam.zoom_to_area(area, 35);
    } else if (has_entry_center_) {
        cam.set_focus_override(entry_center_);
        cam.zoom_to_scale(1.0, 20);
    } else if (has_spawn_center) {
        cam.set_focus_override(spawn_center);
        if (spawn_room && spawn_room->room_area) {
            Area adjusted = cam.convert_area_to_aspect(*spawn_room->room_area);
            cam.zoom_to_area(adjusted, 35);
        } else {
            cam.zoom_to_scale(1.0, 20);
        }
    } else {
        cam.set_focus_override(SDL_Point{0, 0});
        cam.zoom_to_scale(1.0, 20);
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
    if (!assets_) return;
    camera& cam = assets_->getView();

    if (focus_player) {
        cam.clear_focus_override();
        cam.set_manual_zoom_override(false);
        return;
    }

    if (!restore_previous_state) {
        return;
    }

    cam.set_manual_zoom_override(prev_manual_override_);
    if (prev_focus_override_) {
        cam.set_focus_override(prev_focus_point_);
    } else {
        cam.clear_focus_override();
    }
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

    SDL_SetRenderDrawColor(renderer, bg_color.r, bg_color.g, bg_color.b, bg_color.a);
    SDL_RenderFillRect(renderer, &bg_rect);
    SDL_SetRenderDrawColor(renderer, border_color.r, border_color.g, border_color.b, border_color.a);
    SDL_RenderDrawRect(renderer, &bg_rect);

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
    rect.x = screen_pos.x - rect_w / 2;
    rect.y = screen_pos.y - rect_h / 2 - kLabelVerticalOffset;

    if (screen_w_ > 0) {
        rect.x = std::max(rect.x, 0);
        rect.x = std::min(rect.x, screen_w_ - rect.w);
    }
    if (screen_h_ > 0) {
        rect.y = std::max(rect.y, 0);
        rect.y = std::min(rect.y, screen_h_ - rect.h);
    }
    return rect;
}
