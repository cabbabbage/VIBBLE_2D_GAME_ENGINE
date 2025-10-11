#pragma once

#include <SDL.h>
#include <vector>
struct SDL_Renderer;

namespace animation_editor {

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

    void update();
    void render(SDL_Renderer* renderer) const;
    bool handle_event(const SDL_Event& e);

  private:
    void rebuild_path();
    void fit_view_to_content();
    void pan_view(float delta_x, float delta_y);
    void apply_zoom(float scale_delta);
    void update_selection_from_mouse();
    SDL_FPoint world_to_screen(const SDL_FPoint& world) const;
    SDL_FPoint screen_to_world(SDL_Point screen) const;

  private:
    SDL_Rect bounds_{0, 0, 0, 0};
    std::vector<MovementFrame> frames_;
    std::vector<SDL_FPoint> positions_;
    float pixels_per_unit_ = 32.0f;
    float zoom_ = 1.0f;
    SDL_FPoint center_world_{0.0f, 0.0f};
    int selected_index_ = 0;
    int hovered_index_ = -1;
    bool dragging_frame_ = false;
    bool panning_ = false;
    SDL_Point last_mouse_{0, 0};
    SDL_Point drag_last_mouse_{0, 0};
    SDL_FPoint drag_target_world_{0.0f, 0.0f};
};

}

