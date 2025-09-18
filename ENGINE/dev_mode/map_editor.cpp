#include "map_editor.hpp"

#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "dev_mode/dm_styles.hpp"
#include "render/camera.hpp"
#include "room/room.hpp"
#include "utils/area.hpp"
#include "utils/input.hpp"

#include <algorithm>
#include <cctype>
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
const SDL_Color kTrailLabelBg{18, 52, 18, 192};
const SDL_Color kTrailLabelBorder{144, 220, 144, 110};
const SDL_Color kTrailLabelText{220, 252, 220, 255};
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
    pending_selection_ = Selection{};

    if (assets_) {
        camera& cam = assets_->getView();
        prev_manual_override_ = cam.is_manual_zoom_override();
        prev_focus_override_ = cam.has_focus_override();
        if (prev_focus_override_) {
            prev_focus_point_ = cam.get_focus_override_point();
        } else {
            prev_focus_point_ = SDL_Point{0, 0};
        }
    }

    compute_bounds();
    apply_camera_to_bounds();
}

void MapEditor::exit(bool focus_player, bool restore_previous_state) {
    if (!enabled_) {
        restore_camera_state(focus_player, restore_previous_state);
        return;
    }
    enabled_ = false;
    restore_camera_state(focus_player, restore_previous_state);
    pending_selection_ = Selection{};
}

void MapEditor::update(const Input& input) {
    if (!enabled_) return;
    if (!assets_) return;

    camera& cam = assets_->getView();

    SDL_Point screen_pt{input.getX(), input.getY()};
    SDL_Point map_pt = cam.screen_to_map(screen_pt);

    Room* area_hit = hit_test_room(map_pt);
    const LabelEntry* label_hit = nullptr;
    for (const auto& entry : label_rects_) {
        if (SDL_PointInRect(&screen_pt, &entry.rect)) {
            label_hit = &entry;
            break;
        }
    }

    Room* hit = area_hit ? area_hit : (label_hit ? label_hit->room : nullptr);

    pan_zoom_.handle_input(cam, input, hit != nullptr);

    if (input.wasClicked(Input::LEFT)) {
        pending_selection_ = Selection{};
        if (label_hit && label_hit->room) {
            pending_selection_.room = label_hit->room;
            pending_selection_.kind = (label_hit->type == LabelType::Trail)
                                          ? Selection::Kind::TrailLabel
                                          : Selection::Kind::RoomLabel;
        } else if (area_hit) {
            pending_selection_.room = area_hit;
            pending_selection_.kind = is_trail_room(area_hit)
                                          ? Selection::Kind::TrailArea
                                          : Selection::Kind::RoomArea;
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
        LabelType type = is_trail_room(room) ? LabelType::Trail : LabelType::Room;
        render_room_label(renderer, room, type);
    }
}

MapEditor::Selection MapEditor::consume_selection() {
    Selection out = pending_selection_;
    pending_selection_ = Selection{};
    return out;
}

void MapEditor::focus_on_room(Room* room) {
    if (!room || !room->room_area || !assets_) return;

    camera& cam = assets_->getView();
    Area adjusted = cam.convert_area_to_aspect(*room->room_area);
    cam.set_manual_zoom_override(true);
    cam.set_focus_override(adjusted.get_center());
    cam.zoom_to_area(adjusted, 35);
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

    if (has_bounds_) {
        int min_x = bounds_.min_x - kBoundsPadding;
        int min_y = bounds_.min_y - kBoundsPadding;
        int max_x = bounds_.max_x + kBoundsPadding;
        int max_y = bounds_.max_y + kBoundsPadding;

        std::vector<SDL_Point> pts{
            {min_x, min_y},
            {max_x, min_y},
            {max_x, max_y},
            {min_x, max_y},
        };
        Area area("map_bounds", pts);
        cam.set_focus_override(area.get_center());
        cam.zoom_to_area(area, 35);
    } else if (assets_->player) {
        SDL_Point center{assets_->player->pos.x, assets_->player->pos.y};
        cam.set_focus_override(center);
        cam.zoom_to_scale(1.0, 20);
    }
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

void MapEditor::render_room_label(SDL_Renderer* renderer, Room* room, LabelType type) {
    if (!room || !room->room_area || !assets_) return;
    if (!label_font_) return;

    std::string name = room->room_name.empty() ? std::string("<unnamed>") : room->room_name;
    SDL_Color text_color = kLabelText;
    SDL_Color bg_color = kLabelBg;
    SDL_Color border_color = kLabelBorder;
    if (type == LabelType::Trail) {
        text_color = kTrailLabelText;
        bg_color = kTrailLabelBg;
        border_color = kTrailLabelBorder;
        name = std::string("Trail: ") + name;
    }

    SDL_Surface* text_surface = TTF_RenderUTF8_Blended(label_font_, name.c_str(), text_color);
    if (!text_surface) return;

    SDL_Point center = room->room_area->get_center();
    SDL_Point screen_pt = assets_->getView().map_to_screen(center);
    SDL_Rect bg_rect = label_background_rect(text_surface, screen_pt);

    label_rects_.push_back(LabelEntry{room, bg_rect, type});

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

bool MapEditor::is_trail_room(const Room* room) const {
    if (!room) return false;
    std::string type = room->type;
    std::transform(type.begin(), type.end(), type.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (type == "trail") {
        return true;
    }
    const std::string& directory = room->room_directory;
    if (directory.find("trails_data") != std::string::npos) {
        return true;
    }
    return false;
}
