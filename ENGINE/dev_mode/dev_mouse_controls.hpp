
#pragma once

#include <vector>
#include <unordered_set>
#include <algorithm>
#include <SDL.h>
#include "utils\parallax.hpp" 

class Asset;
class MouseInput;
class Parallax;

class DevMouseControls {
public:
    DevMouseControls(MouseInput* m,
                     std::vector<Asset*>& actives,
                     Asset* player,
                     int screen_w,
                     int screen_h);

    void handle_mouse_input(const std::unordered_set<SDL_Keycode>& keys);
    void handle_hover();
    void handle_click(const std::unordered_set<SDL_Keycode>& keys);
    void update_highlighted_assets();

    const std::vector<Asset*>& get_selected_assets() const { return selected_assets; }
    const std::vector<Asset*>& get_highlighted_assets() const { return highlighted_assets; }
    Asset* get_hovered_asset() const { return hovered_asset; }

private:
    SDL_Point compute_mouse_world(int mx_screen, int my_screen) const;
    bool dragging_;
    int drag_last_x_, drag_last_y_;

private:
    int click_buffer_frames_ = 0;
    int hover_miss_frames_ = 0;
    MouseInput* mouse;
    std::vector<Asset*>& active_assets;
    Asset* player;
    int screen_w;
    int screen_h;

    Parallax parallax_;  

    Asset* hovered_asset = nullptr;
    std::vector<Asset*> selected_assets;
    std::vector<Asset*> highlighted_assets;
};
