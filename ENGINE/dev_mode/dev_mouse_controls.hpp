#pragma once

#include <vector>
#include <unordered_set>
#include <algorithm>
#include <SDL.h>
#include "render/camera.hpp"

class Asset;
class Input;
class Assets;

class DevMouseControls {

	public:
    DevMouseControls(Input* m, Assets* assets, std::vector<Asset*>& actives, Asset* player, int screen_w, int screen_h);
    void handle_mouse_input(const Input& input);
    void handle_hover();
    void handle_click(const Input& input);
    void update_highlighted_assets();
    void purge_asset(Asset* a);
    void reset_click_state() {
    click_buffer_frames_ = 0;
    rclick_buffer_frames_ = 0;
    last_click_time_ms_ = 0;
    last_click_asset_ = nullptr;
    dragging_ = false;
        }
    const std::vector<Asset*>& get_selected_assets() const { return selected_assets; }
    const std::vector<Asset*>& get_highlighted_assets() const { return highlighted_assets; }
    Asset* get_hovered_asset() const { return hovered_asset; }

    // Clear all current selection/highlight state
    void clear_selection();
    
    // Zoom control configuration
    void set_zoom_scale_factor(double f) { zoom_scale_factor_ = (f > 0.0) ? f : 1.0; }
    double get_zoom_scale_factor() const { return zoom_scale_factor_; }

	private:
    SDL_Point compute_mouse_world(int mx_screen, int my_screen) const;
    bool dragging_;
    int drag_last_x_, drag_last_y_;
    Asset* drag_anchor_asset_ = nullptr;
    Uint32 last_click_time_ms_ = 0;
    Asset* last_click_asset_ = nullptr;

        private:
    int click_buffer_frames_ = 0;
    int rclick_buffer_frames_ = 0;
    int hover_miss_frames_ = 0;
    Input* mouse;
    Assets* assets_ = nullptr;
    std::vector<Asset*>& active_assets;
    Asset* player;
    int screen_w;
    int screen_h;
    
    Asset* hovered_asset = nullptr;
    std::vector<Asset*> selected_assets;
    std::vector<Asset*> highlighted_assets;
    // Asset library spawns handled by AssetLibraryUI (floating panel)

    // Zoom configuration
    double zoom_scale_factor_ = 1.1; // multiplicative factor per scroll step
};

