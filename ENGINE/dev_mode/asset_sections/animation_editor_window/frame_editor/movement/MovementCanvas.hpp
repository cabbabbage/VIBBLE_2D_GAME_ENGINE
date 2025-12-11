#pragma once

#include <SDL.h>
#include <vector>
#include <algorithm>
#include <memory>
#include <string>
struct SDL_Renderer;

namespace animation_editor {

class PreviewProvider;

struct MovementFrame {
    float dx = 0.0f;
    float dy = 0.0f;
    bool resort_z = false;

};

class MovementCanvas {
  public:
    MovementCanvas();

    void set_bounds(const SDL_Rect& bounds);
    void set_frames(const std::vector<MovementFrame>& frames, bool preserve_view = false);
    const std::vector<MovementFrame>& frames() const { return frames_; }
    void set_selected_index(int index);
    int selected_index() const { return selected_index_; }
    int hovered_index() const { return hovered_index_; }

    void update();
    void render(SDL_Renderer* renderer) const;

    void render_background(SDL_Renderer* renderer) const;
    bool handle_event(const SDL_Event& e);

    void set_animation_context(std::shared_ptr<PreviewProvider> provider, const std::string& animation_id, float scale_percentage);
    void set_show_animation_overlay(bool show) { show_animation_overlay_ = show; }

    void set_snap_resolution(int r) { snap_resolution_ = r; }

    void set_anchor_follows_movement(bool follow) { anchor_follows_movement_ = follow; }

    void set_smoothing_enabled(bool enabled) { smoothing_enabled_ = enabled; }
    void set_smoothing_curve_enabled(bool enabled) { smoothing_curve_enabled_ = enabled; }
    const SDL_Rect& bounds() const { return bounds_; }
    SDL_FPoint world_to_screen(const SDL_FPoint& world) const;
    SDL_FPoint screen_to_world(SDL_Point screen) const;
    float screen_pixels_per_unit() const;
    float document_scale_factor() const;
    SDL_FPoint frame_position_world(int frame_index) const;
    SDL_FPoint frame_anchor_world(int frame_index) const;
    SDL_FPoint frame_anchor_screen(int frame_index) const;

  private:
    void render_pixel_grid(SDL_Renderer* renderer) const;
    void rebuild_path();
    void fit_view_to_content();
    void pan_view(float delta_x, float delta_y);
    void apply_zoom(float scale_delta);
    void apply_frame_move_from_base(int index, const SDL_FPoint& new_position, const std::vector<SDL_FPoint>& base_positions);
    void update_selection_from_mouse();

  private:
    SDL_Rect bounds_{0, 0, 0, 0};
    std::vector<MovementFrame> frames_;
    std::vector<SDL_FPoint> positions_;
    float pixels_per_unit_ = 1.0f;
    float zoom_ = 1.0f;
    SDL_FPoint center_world_{0.0f, 0.0f};
    int selected_index_ = 0;
    int hovered_index_ = -1;
    bool dragging_frame_ = false;
    bool panning_ = false;
    SDL_Point last_mouse_{0, 0};
    SDL_Point drag_last_mouse_{0, 0};
    SDL_FPoint drag_target_world_{0.0f, 0.0f};
    std::vector<SDL_FPoint> drag_base_positions_;

    std::shared_ptr<PreviewProvider> preview_provider_;
    std::string animation_id_;
    bool show_animation_overlay_ = false;
    float base_scale_percentage_ = 100.0f;
    int snap_resolution_ = -1;
    bool anchor_follows_movement_ = true;
    bool smoothing_enabled_ = false;
    bool smoothing_curve_enabled_ = false;
};

}
