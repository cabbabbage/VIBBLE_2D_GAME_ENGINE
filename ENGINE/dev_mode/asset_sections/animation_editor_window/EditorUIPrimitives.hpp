#pragma once

#include <SDL.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

namespace animation_editor::ui {

struct PanelMetrics {
    int padding = 12;
    int gap = 6;
    int section_gap = 12;
};

class WidgetRegistry {
  public:
    void reset() { handlers_.clear(); }

    template <typename WidgetT>
    WidgetT* track(WidgetT* widget) {
        if (!widget) {
            return nullptr;
        }
        handlers_.push_back([widget](const SDL_Event& e) {
            return widget->handle_event(e);
        });
        return widget;
    }

    template <typename WidgetT>
    WidgetT* track(const std::unique_ptr<WidgetT>& widget) {
        return track(widget.get());
    }

    void add_handler(std::function<bool(const SDL_Event&)> handler) {
        if (handler) {
            handlers_.push_back(std::move(handler));
        }
    }

    bool handle_event(const SDL_Event& e) const;

  private:
    std::vector<std::function<bool(const SDL_Event&)>> handlers_;
};

class ScrollController {
  public:
    void set_bounds(const SDL_Rect& bounds);
    void set_content_height(int height);
    void set_step_pixels(int step);
    void set_scroll(int value);
    int scroll() const { return scroll_; }
    SDL_Rect apply(const SDL_Rect& rect) const;
    bool handle_wheel(const SDL_Event& e);
    void clamp();
    bool apply_wheel_delta(int delta_lines);

  private:
    SDL_Rect bounds_{0, 0, 0, 0};
    int content_height_ = 0;
    int scroll_ = 0;
    int step_pixels_ = 20;
};

void draw_panel_background(SDL_Renderer* renderer, const SDL_Rect& bounds);

}
