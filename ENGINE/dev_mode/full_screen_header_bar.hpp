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

class FullScreenHeaderBar {
public:
    struct HeaderButton {
        std::string id;
        std::string label;
        bool active = false;
        std::function<void(bool active)> on_toggle;
        bool momentary = false;

        const DMButtonStyle* style_override = nullptr;
        const DMButtonStyle* active_style_override = nullptr;
        std::unique_ptr<DMButton> widget;
    };

    explicit FullScreenHeaderBar(std::string title);

    void set_title(const std::string& title);
    void set_title_visible(bool visible);
    bool title_visible() const { return show_title_; }

    void set_bounds(int width, int height);
    void set_header_height(int height);

    void set_visible(bool visible) { visible_ = visible; }
    bool visible() const { return visible_; }

    void set_expanded(bool expanded);
    bool expanded() const { return expanded_; }

    void set_on_toggle(std::function<void(bool)> cb) { on_toggle_ = std::move(cb); }

    void set_header_buttons(std::vector<HeaderButton> buttons);
    void activate_button(const std::string& id);
    void set_active_button(const std::string& id, bool trigger_callback = false);
    void set_button_active_state(const std::string& id, bool active);

    void update(const Input& input);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* renderer) const;

    const std::vector<HeaderButton>& header_buttons() const { return buttons_; }
    const HeaderButton* find_button(const std::string& id) const;

    const SDL_Rect& header_rect() const { return header_rect_; }
    bool contains(int x, int y) const;

private:
    void layout();
    void layout_buttons();
    void update_title_width();

private:
    std::string title_;
    int screen_w_ = 0;
    int screen_h_ = 0;
    int header_height_ = 0;
    bool visible_ = true;
    bool expanded_ = false;
    bool show_title_ = true;

    SDL_Rect header_rect_{0, 0, 0, 0};
    int title_width_ = 0;

    std::unique_ptr<DMButton> arrow_button_;
    std::vector<HeaderButton> buttons_;

    std::function<void(bool)> on_toggle_;
};

