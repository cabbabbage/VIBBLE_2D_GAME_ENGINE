#pragma once

#include <SDL.h>
#include <algorithm>
#include "utils/area.hpp"

class Asset;
class Room;
class CurrentRoomFinder;

class view {
public:
    struct Bounds {
        int left;
        int right;
        int top;
        int bottom;
    };
    view(int screen_width, int screen_height, const Bounds& starting_bounds);
    void set_scale(float s);
    float get_scale() const;
    Bounds get_base_bounds() const;
    Bounds get_current_bounds() const;
    SDL_Rect to_world_rect(int cx, int cy) const;
    Area get_view_area(int cx, int cy) const;
    bool is_point_in_bounds(int x, int y, int cx, int cy) const;
    bool is_asset_in_bounds(const Asset& a, int cx, int cy) const;
    void zoom_scale(double target_scale, int duration_steps);
    void zoom_bounds(const Bounds& target_bounds, int duration_steps);
    void update();
    void set_up_rooms(CurrentRoomFinder* finder);
    void update_zoom(Room* cur, CurrentRoomFinder* finder, Asset* player);
    bool intro = true;
    bool   zooming_      = false;
private:
    Bounds base_bounds_{};
    Bounds current_bounds_{};
    float  scale_ = 1.0f;
    double start_scale_  = 1.0;
    double target_scale_ = 1.0;
    int    steps_total_  = 0;
    int    steps_done_   = 0;
    
    Room*  starting_room_ = nullptr;
    double starting_area_ = 1.0;
    double compute_room_scale_from_area(const Room* room) const;
};
