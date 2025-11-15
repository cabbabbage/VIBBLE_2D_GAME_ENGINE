#pragma once

#include <SDL.h>
#include <functional>
#include <memory>
#include <string>

class Input;
class DMButton;
struct DMButtonStyle;

class SlidingWindowContainer {
public:
    struct LayoutContext {
        int content_x;
        int content_width;
        int scroll_value;
        int content_top;
        int gap;
};

    using LayoutFunction = std::function<int(const LayoutContext&)>;
    using RenderFunction = std::function<void(SDL_Renderer*)>;
    using UpdateFunction = std::function<void(const Input&, int, int)>;
    using EventFunction = std::function<bool(const SDL_Event&)>;
    using HeaderTextProvider = std::function<std::string()>;

    SlidingWindowContainer();

    void set_layout_function(LayoutFunction fn);
    void set_render_function(RenderFunction fn);
    void set_update_function(UpdateFunction fn);
    void set_event_function(EventFunction fn);
    void set_header_text(const std::string& text);
    void set_header_text_provider(HeaderTextProvider provider);
    void set_on_close(std::function<void()> cb);
    void set_header_visible(bool visible);
    void set_close_button_enabled(bool enabled);
    void set_scrollbar_visible(bool visible);
    void set_header_navigation_button(const std::string& label, std::function<void()> on_click, const DMButtonStyle* style = nullptr);
    void clear_header_navigation_button();
    void set_header_navigation_alignment_right(bool align_right);
    void set_content_clip_enabled(bool enabled);

    void request_layout();

    void set_blocks_editor_interactions(bool block);
    void set_editor_interaction_blocker(std::function<void(bool)> blocker);
    void set_header_visibility_controller(std::function<void(bool)> controller);

    void set_panel_bounds_override(const SDL_Rect& bounds);
    void clear_panel_bounds_override();

    void open();
    void close();
    void set_visible(bool visible);
    bool is_visible() const { return visible_; }

    void reset_scroll();
    void pulse_header();
    int scroll_value() const;
    void set_scroll_value(int value);

    void update(const Input& input, int screen_w, int screen_h);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* renderer, int screen_w, int screen_h) const;

    void prepare_layout(int screen_w, int screen_h) const;

    const SDL_Rect& panel_rect() const { return panel_; }
    const SDL_Rect& scroll_region() const { return scroll_region_; }

    bool is_point_inside(int x, int y) const;

private:
    void layout(int screen_w, int screen_h) const;
    void update_scroll_from_delta(int delta);
    void update_editor_interaction_block_state();

private:
    LayoutFunction layout_function_{};
    RenderFunction render_function_{};
    UpdateFunction update_function_{};
    EventFunction event_function_{};
    HeaderTextProvider header_text_provider_{};
    std::string header_text_{};

    std::function<void()> on_close_{};
    std::function<void(bool)> editor_interaction_blocker_{};
    std::function<void(bool)> header_visibility_controller_{};

    bool visible_ = false;
    bool header_visible_ = true;
    bool close_button_enabled_ = true;
    bool blocks_editor_interactions_ = false;
    bool editor_interactions_blocked_ = false;

    bool panel_override_active_ = false;
    SDL_Rect panel_override_{0, 0, 0, 0};

    bool scrollbar_visible_ = false;
    bool content_clip_enabled_ = true;

    mutable SDL_Rect panel_{0,0,0,0};
    mutable SDL_Rect name_label_rect_{0,0,0,0};
    mutable SDL_Rect close_button_rect_{0,0,0,0};
    mutable SDL_Rect header_nav_rect_{0,0,0,0};
    mutable SDL_Rect content_clip_rect_{0,0,0,0};
    mutable SDL_Rect scroll_region_{0,0,0,0};
    mutable SDL_Rect scroll_track_rect_{0,0,0,0};
    mutable SDL_Rect scroll_thumb_rect_{0,0,0,0};

    mutable int scroll_ = 0;
    mutable int max_scroll_ = 0;
    mutable int content_height_px_ = 0;
    mutable int visible_height_px_ = 0;
    mutable bool scroll_dragging_ = false;
    mutable bool scrollbar_dragging_ = false;
    mutable int scroll_drag_anchor_y_ = 0;
    mutable int scroll_drag_start_scroll_ = 0;
    mutable int scrollbar_drag_offset_ = 0;
    mutable int pulse_frames_ = 0;

    mutable std::unique_ptr<DMButton> close_button_{};
    mutable std::unique_ptr<DMButton> header_nav_button_{};
    std::function<void()> header_nav_callback_{};
    bool header_nav_align_right_ = false;

    mutable int last_screen_w_ = 0;
    mutable int last_screen_h_ = 0;
    mutable bool layout_dirty_ = true;
};

