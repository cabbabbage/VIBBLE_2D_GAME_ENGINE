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

    Room* consume_selected_room();
    void focus_on_room(Room* room);

private:
    void ensure_font();
    void release_font();
    bool compute_bounds();
    void apply_camera_to_bounds();
    void restore_camera_state(bool focus_player, bool restore_previous_state);
    Room* hit_test_room(SDL_Point map_point) const;
    void render_room_label(SDL_Renderer* renderer, Room* room);
    SDL_Rect label_background_rect(const SDL_Surface* surface, SDL_Point screen_pos) const;

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

    TTF_Font* label_font_ = nullptr;

    Room* pending_selection_ = nullptr;
    PanAndZoom pan_zoom_;
    std::vector<std::pair<Room*, SDL_Rect>> label_rects_;
};
