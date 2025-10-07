#pragma once

#include <SDL.h>
#include <functional>
#include <vector>

struct SDL_Renderer;

namespace animation_editor {

struct MovementFrame;

class TotalsPanel {
  public:
    TotalsPanel();

    void set_bounds(const SDL_Rect& bounds);
    void set_frames(const std::vector<MovementFrame>& frames);
    void set_selected_index(int* selected_index);
    void set_on_selection_changed(std::function<void(int)> callback);

    void update();
    void render(SDL_Renderer* renderer) const;
    bool handle_event(const SDL_Event& e);

  private:
    void recalculate_totals();

  private:
    SDL_Rect bounds_{0, 0, 0, 0};
    SDL_Rect prev_button_{0, 0, 0, 0};
    SDL_Rect next_button_{0, 0, 0, 0};
    std::vector<MovementFrame> frames_;
    float total_dx_ = 0.0f;
    float total_dy_ = 0.0f;
    int* selected_index_ = nullptr;
    std::function<void(int)> on_selection_changed_;
};

}  // namespace animation_editor

