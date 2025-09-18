#pragma once

#include <SDL.h>
#include <SDL_ttf.h>

#include <vector>
#include <utility>

#include "dev_mode/pan_and_zoom.hpp"

class Assets;
class Input;
class Room;

class MapEditor {
public:
    explicit MapEditor(Assets* owner);
    ~MapEditor();

    void set_input(Input* input);
    void set_rooms(std::vector<Room*>* rooms);
    void set_screen_dimensions(int width, int height);

    void enter();
    void exit(bool focus_player, bool restore_previous_state = true);

    void set_enabled(bool enabled);
    bool is_enabled() const { return enabled_; }

    void update(const Input& input);
    void render(SDL_Renderer* renderer);

    struct Selection {
        enum class Kind {
            None,
            RoomArea,
            RoomLabel,
            TrailArea,
            TrailLabel,
        };
        Room* room = nullptr;
        Kind kind = Kind::None;
        bool is_trail() const {
            return kind == Kind::TrailArea || kind == Kind::TrailLabel;
        }
    };

    Selection consume_selection();
    void focus_on_room(Room* room);

private:
    void ensure_font();
    void release_font();
    bool compute_bounds();
    void apply_camera_to_bounds();
    void restore_camera_state(bool focus_player, bool restore_previous_state);
    Room* hit_test_room(SDL_Point map_point) const;
    enum class LabelType {
        Room,
        Trail,
    };

    void render_room_label(SDL_Renderer* renderer, Room* room, LabelType type);
    SDL_Rect label_background_rect(const SDL_Surface* surface, SDL_Point screen_pos) const;
    bool is_trail_room(const Room* room) const;
    Room* find_spawn_room() const;

private:
    Assets* assets_ = nullptr;
    Input* input_ = nullptr;
    std::vector<Room*>* rooms_ = nullptr;

    int screen_w_ = 0;
    int screen_h_ = 0;

    bool enabled_ = false;

    struct Bounds {
        int min_x = 0;
        int min_y = 0;
        int max_x = 0;
        int max_y = 0;
    };

    bool has_bounds_ = false;
    Bounds bounds_{};

    bool prev_manual_override_ = false;
    bool prev_focus_override_ = false;
    SDL_Point prev_focus_point_{0, 0};
    bool has_entry_center_ = false;
    SDL_Point entry_center_{0, 0};

    TTF_Font* label_font_ = nullptr;

    Selection pending_selection_{};
    PanAndZoom pan_zoom_;
    struct LabelEntry {
        Room* room = nullptr;
        SDL_Rect rect{0, 0, 0, 0};
        LabelType type = LabelType::Room;
    };
    std::vector<LabelEntry> label_rects_;
};
