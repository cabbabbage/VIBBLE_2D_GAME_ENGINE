
#pragma once

#include <vector>
#include <unordered_set>
#include <algorithm>
#include <SDL.h>
#include "utils/parallax.hpp" 

class Asset;
class Input;
class Parallax;
class Assets; // fwd

class DevMouseControls {
public:
    DevMouseControls(Input* m,
                     Assets* assets,
                     std::vector<Asset*>& actives,
                     Asset* player,
                     int screen_w,
                     int screen_h);

    void handle_mouse_input(const Input& input);
    void handle_hover();
    void handle_click(const Input& input);
    void update_highlighted_assets();
    // Remove references to a destroyed asset from hover/selection state
    void purge_asset(Asset* a);
    // Ensure a single logical click is handled only once across frames
    // and reset state when switching modes.
    void reset_click_state() {
        click_buffer_frames_ = 0;
        last_click_time_ms_ = 0;
        last_click_asset_ = nullptr;
        dragging_ = false;
    }

    const std::vector<Asset*>& get_selected_assets() const { return selected_assets; }
    const std::vector<Asset*>& get_highlighted_assets() const { return highlighted_assets; }
    Asset* get_hovered_asset() const { return hovered_asset; }

private:
    SDL_Point compute_mouse_world(int mx_screen, int my_screen) const;
    bool dragging_;
    int drag_last_x_, drag_last_y_;
    Asset* drag_anchor_asset_ = nullptr; // asset under cursor when drag starts

    // Double-click detection
    Uint32 last_click_time_ms_ = 0;
    Asset* last_click_asset_ = nullptr;

private:
    int click_buffer_frames_ = 0;
    int hover_miss_frames_ = 0;
    Input* mouse;
    Assets* assets_ = nullptr;
    std::vector<Asset*>& active_assets;
    Asset* player;
    int screen_w;
    int screen_h;

    Parallax parallax_;  

    Asset* hovered_asset = nullptr;
    std::vector<Asset*> selected_assets;
    std::vector<Asset*> highlighted_assets;

    // Spawn via right-click + asset selection
    bool waiting_spawn_selection_ = false;
    int spawn_click_screen_x_ = 0;
    int spawn_click_screen_y_ = 0;
    int spawn_world_x_ = 0;
    int spawn_world_y_ = 0;
};
