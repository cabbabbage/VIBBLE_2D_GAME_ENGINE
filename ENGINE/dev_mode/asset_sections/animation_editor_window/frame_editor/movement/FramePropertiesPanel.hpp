#pragma once

#include <functional>
#include <vector>
#include <SDL.h>

struct SDL_Renderer;

#include "MovementCanvas.hpp"

namespace animation_editor {

class MovementCanvas;

class FramePropertiesPanel {
  public:
    FramePropertiesPanel();

    void set_bounds(const SDL_Rect& bounds);
    void set_frames(std::vector<MovementFrame>* frames);
    void set_selected_index(int* selected_index);
    void set_on_frame_changed(std::function<void()> callback);
    void refresh_from_selection();
    bool take_dirty_flag();

    void update();
    void render(SDL_Renderer* renderer) const;
    bool handle_event(const SDL_Event& e);

  private:
    void layout_controls();
    void sync_from_selected();
    void apply_to_selected();

  private:
    SDL_Rect bounds_{0, 0, 0, 0};
    SDL_Rect resort_toggle_rect_{0, 0, 0, 0};
    std::vector<MovementFrame>* frames_ = nullptr;
    int* selected_index_ = nullptr;
    MovementFrame cached_frame_{};
    int cached_index_ = -1;
    bool dirty_ = false;
    std::function<void()> on_frame_changed_;
};

}
