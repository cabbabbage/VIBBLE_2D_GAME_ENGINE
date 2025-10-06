#pragma once

#include <vector>

struct SDL_Rect;
struct SDL_Event;
struct SDL_Renderer;

namespace animation_editor {

struct MovementFrame;

class FramePropertiesPanel {
  public:
    FramePropertiesPanel();

    void set_bounds(const SDL_Rect& bounds);
    void set_frames(std::vector<MovementFrame>* frames);
    void set_selected_index(int* selected_index);

    void update();
    void render(SDL_Renderer* renderer) const;
    bool handle_event(const SDL_Event& e);

  private:
    void sync_from_selected();
    void apply_to_selected();

  private:
    SDL_Rect bounds_{0, 0, 0, 0};
    std::vector<MovementFrame>* frames_ = nullptr;
    int* selected_index_ = nullptr;
    // TODO: Store editable tint color and resort-z state for UI controls.
};

}  // namespace animation_editor

