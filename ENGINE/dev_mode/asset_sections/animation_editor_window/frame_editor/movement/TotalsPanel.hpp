#pragma once

#include <SDL.h>
#include <vector>
#include <memory>
#include <functional>

struct SDL_Renderer;

class DMTextBox;

namespace animation_editor {

struct MovementFrame;

class TotalsPanel {
  public:
    TotalsPanel();

    void set_bounds(const SDL_Rect& bounds);
    void set_frames(const std::vector<MovementFrame>& frames);
    void set_selected_index(const int* selected_index);
    void set_on_totals_changed(std::function<void(int , int )> cb) { on_totals_changed_ = std::move(cb); }

    void update();
    void render(SDL_Renderer* renderer) const;
    bool handle_event(const SDL_Event& e);

  private:
    void recalculate_totals();

  private:
    SDL_Rect bounds_{0, 0, 0, 0};
    std::vector<MovementFrame> frames_;
    float total_dx_ = 0.0f;
    float total_dy_ = 0.0f;
    const int* selected_index_ = nullptr;

    std::unique_ptr<DMTextBox> dx_box_;
    std::unique_ptr<DMTextBox> dy_box_;
    std::function<void(int,int)> on_totals_changed_{};
};

}
