#pragma once

#include <SDL.h>

#include <functional>
#include <string>
#include <vector>

namespace animation_editor {

class AnimationListContextMenu {
  public:
    struct Option {
        std::string label;
        std::function<void()> callback;
};

    AnimationListContextMenu();

    void open(const SDL_Rect& parent_bounds, const SDL_Point& anchor, std::vector<Option> options);
    void close();

    bool is_open() const { return open_; }
    SDL_Rect bounds() const { return rect_; }

    void render(SDL_Renderer* renderer) const;
    bool handle_event(const SDL_Event& e);

  private:
    int option_height() const;
    SDL_Rect option_rect(int index) const;
    int option_index_at_point(SDL_Point p) const;

    bool open_ = false;
    SDL_Rect rect_{0, 0, 0, 0};
    std::vector<Option> options_;
    int hovered_index_ = -1;
    int pressed_index_ = -1;
};

}

