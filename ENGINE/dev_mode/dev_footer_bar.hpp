#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <SDL.h>

#include "dm_styles.hpp"
#include "widgets.hpp"

class Input;

class DevFooterBar {
public:
    struct Button {
        std::string id;
        std::string label;
        bool active = false;
        std::function<void(bool)> on_toggle;
        bool momentary = false;

        const DMButtonStyle* style_override = nullptr;
        const DMButtonStyle* active_style_override = nullptr;
        std::unique_ptr<DMButton> widget;
};

    explicit DevFooterBar(std::string title);

    void set_title(const std::string& title);
    void set_title_visible(bool visible);
    bool title_visible() const { return show_title_; }

    void set_bounds(int width, int height);
    void set_height(int height);

    void set_visible(bool visible) { visible_ = visible; }
    bool visible() const { return visible_; }

    void set_buttons(std::vector<Button> buttons);
    void activate_button(const std::string& id);
    void set_active_button(const std::string& id, bool trigger_callback = false);
    void set_button_active_state(const std::string& id, bool active);

    void update(const Input& input);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* renderer) const;

    const std::vector<Button>& buttons() const { return buttons_; }
    const Button* find_button(const std::string& id) const;

    const SDL_Rect& rect() const { return rect_; }
    bool contains(int x, int y) const;

private:
    void layout();
    void layout_buttons();
    void update_title_width();

    std::string title_;
    int screen_w_ = 0;
    int screen_h_ = 0;
    int height_ = 0;
    bool visible_ = true;
    bool show_title_ = true;

    SDL_Rect rect_{0, 0, 0, 0};
    int title_width_ = 0;

    std::vector<Button> buttons_;
};

