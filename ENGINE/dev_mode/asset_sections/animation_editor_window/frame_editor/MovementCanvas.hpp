#pragma once

#include <vector>

struct SDL_Rect;
struct SDL_Event;
struct SDL_Renderer;

namespace animation_editor {

struct MovementFrame {
    float dx = 0.0f;
    float dy = 0.0f;
    bool resort_z = false;
    // TODO: Add tint metadata fields when implemented.
};

class MovementCanvas {
  public:
    MovementCanvas();

    void set_bounds(const SDL_Rect& bounds);
    void set_frames(const std::vector<MovementFrame>& frames);
    const std::vector<MovementFrame>& frames() const { return frames_; }

    void update();
    void render(SDL_Renderer* renderer) const;
    bool handle_event(const SDL_Event& e);

  private:
    void pan_view(float delta_x, float delta_y);
    void apply_zoom(float scale_delta);
    void update_selection_from_mouse();

  private:
    SDL_Rect bounds_{0, 0, 0, 0};
    std::vector<MovementFrame> frames_;
    // TODO: Track zoom level, pan offset, and selected frame index.
};

}  // namespace animation_editor

