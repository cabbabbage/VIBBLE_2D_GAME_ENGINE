#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <SDL.h>

#include "dm_styles.hpp"
#include "widgets.hpp"

class Input;

// Full-screen variant of DockableCollapsible that pins its header to the
// bottom of the screen when collapsed and expands to reveal a content region
// stretching to the top when opened.
class FullScreenCollapsible {
public:
    struct HeaderButton {
        std::string id;
        std::string label;
        bool active = false;
        std::function<void(bool active)> on_toggle;
        bool momentary = false;
        // Optional style override for this button (defaults to HeaderButton style)
        const DMButtonStyle* style_override = nullptr;
        std::unique_ptr<DMButton> widget;
    };

    explicit FullScreenCollapsible(std::string title);

    void set_bounds(int width, int height);

    void set_header_height(int height);

    void set_visible(bool visible) { visible_ = visible; }
    bool visible() const { return visible_; }

    void set_expanded(bool expanded);
    bool expanded() const { return expanded_; }

    // Callback invoked whenever the expanded/collapsed state changes via the
    // arrow button.
    void set_on_toggle(std::function<void(bool)> cb) { on_toggle_ = std::move(cb); }

    // Optional callback that receives events targeting the expanded content
    // area. If provided, handle_event() will invoke it before deciding
    // whether to swallow the event itself.
    void set_content_event_handler(std::function<bool(const SDL_Event&)> cb) {
        content_event_handler_ = std::move(cb);
    }

    // Header buttons are mutually exclusive. Setting them replaces any
    // existing buttons. The provided callbacks are invoked when a button is
    // toggled active.
    void set_header_buttons(std::vector<HeaderButton> buttons);
    void activate_button(const std::string& id);
    void set_active_button(const std::string& id, bool trigger_callback = false);
    void set_button_active_state(const std::string& id, bool active);

    // Update input state (for hover animations).
    void update(const Input& input);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* renderer) const;

    const std::vector<HeaderButton>& header_buttons() const { return buttons_; }
    const HeaderButton* find_button(const std::string& id) const;

    const SDL_Rect& header_rect() const { return header_rect_; }
    const SDL_Rect& content_rect() const { return content_rect_; }
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

    SDL_Rect header_rect_{0,0,0,0};
    SDL_Rect content_rect_{0,0,0,0};
    int title_width_ = 0;

    std::unique_ptr<DMButton> arrow_button_;
    std::vector<HeaderButton> buttons_;

    std::function<void(bool)> on_toggle_;
    std::function<bool(const SDL_Event&)> content_event_handler_;
};

