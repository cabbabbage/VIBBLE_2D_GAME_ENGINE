#pragma once

#include <SDL.h>
#include <memory>
#include <string>
#include <vector>

#include "dm_styles.hpp"
#include "widgets.hpp"

class Input; // fwd
class AssetInfo; // fwd

// Collapsible container that can either float (draggable) or be docked
// in a fixed rectangle. Used for dev-mode panels and AssetInfo sections.
// - Rows: std::vector<std::vector<Widget*>>; each inner vector is a row
// - Each row uses even column widths; row height = tallest widget in row
// - Auto sizes width to the widest row when floating, otherwise uses
//   the provided docked width
// - Scrolls when content exceeds available height
// - Header shows title + arrow and optionally includes a drag handle area
// - Designed to render behind AssetInfoUI (call render() before AssetInfoUI)
class DockableCollapsible {
public:
    using Row = std::vector<Widget*>;
    using Rows = std::vector<Row>;

    explicit DockableCollapsible(const std::string& title, bool floatable = true,
                                int x = 32, int y = 32);
    virtual ~DockableCollapsible();

    // Basic metadata
    void set_title(const std::string& title);
    virtual void set_info(const std::shared_ptr<AssetInfo>& info) { info_ = info; }
    virtual void build() {}

    // Content rows are non-owning Widget* wrappers (see ui_widget.hpp adapters)
    void set_rows(const Rows& rows);

    // Visibility and collapse state
    bool is_visible() const { return visible_; }
    void set_visible(bool v);
    bool is_expanded() const { return expanded_; }
    void set_expanded(bool e);

    // Header visibility (when hidden, the panel remains expanded and only the
    // body content is shown).
    void set_show_header(bool show);
    bool show_header() const { return show_header_; }

    // Allow showing a close button even when the panel is not floatable
    void set_close_button_enabled(bool enabled);

    // Enable or disable scroll handling independent of floatable state.
    void set_scroll_enabled(bool enabled) { scroll_enabled_ = enabled; }
    bool scroll_enabled() const { return scroll_enabled_; }

    // Override the computed available body height when expanded (use -1 to
    // restore the default behaviour).
    void set_available_height_override(int height) { available_height_override_ = height; }

    // Position and bounds (work area is used for clamping and available height)
    void set_position(int x, int y);              // used when floating
    void set_rect(const SDL_Rect& r);             // used when docked
    SDL_Point position() const { return SDL_Point{rect_.x, rect_.y}; }
    void set_work_area(const SDL_Rect& area); // e.g., full screen or a sub-rect

    // Layout configuration
    void set_cell_width(int w) { cell_width_ = std::max(40, w); }
    void set_padding(int p)    { padding_ = std::max(0, p); }
    void set_row_gap(int g)    { row_gap_ = std::max(0, g); }
    void set_col_gap(int g)    { col_gap_ = std::max(0, g); }
    void set_visible_height(int h) { visible_height_ = std::max(0, h); }

    // Reset internal scroll position
    void reset_scroll() const { scroll_ = 0; }

    // Event/update/render
    virtual void update(const Input& input, int screen_w, int screen_h);
    virtual bool handle_event(const SDL_Event& e);
    virtual void render(SDL_Renderer* r) const;
    virtual void render_content(SDL_Renderer* r) const {}

    // Rect of the whole floating panel
    const SDL_Rect& rect() const { return rect_; }
    int height() const { return rect_.h; }
    bool is_point_inside(int x, int y) const;

    void set_on_close(std::function<void()> cb) { on_close_ = std::move(cb); }

private:
    void layout(int screen_w, int screen_h) const;
    void update_header_button() const;
    int  compute_row_width(int num_cols) const;
    int  available_height(int screen_h) const;
    void clamp_to_bounds(int screen_w, int screen_h) const;

protected:
    virtual void layout();

protected:
    std::string title_;
    mutable std::unique_ptr<DMButton> header_btn_;
    mutable std::unique_ptr<DMButton> close_btn_;
    mutable SDL_Rect rect_{32,32,260,DMButton::height()+8};
    mutable SDL_Rect header_rect_{0,0,0,0};
    mutable SDL_Rect handle_rect_{0,0,0,0};
    mutable SDL_Rect close_rect_{0,0,0,0};
    mutable SDL_Rect body_viewport_{0,0,0,0};

    Rows rows_;
    mutable std::vector<int> row_heights_;
    mutable int content_height_ = 0;  // full content height (unclipped)
    mutable int widest_row_w_ = 0;    // computed panel width
    mutable int body_viewport_h_ = 0; // visible body height (clipped)
    int visible_height_ = 400;        // docked mode max body height

    // Interaction
    bool visible_ = true;
    bool expanded_ = false;
    bool floatable_ = true;
    bool close_button_enabled_ = false;
    bool dragging_ = false;
    bool header_dragging_via_button_ = false;
    bool header_btn_drag_moved_ = false;
    SDL_Point drag_offset_{0,0};
    mutable int scroll_ = 0;
    mutable int max_scroll_ = 0;
    std::shared_ptr<AssetInfo> info_{};

    // Layout config
    int padding_   = 10;   // outer padding
    int row_gap_   = 8;    // vertical gap between rows
    int col_gap_   = 12;   // horizontal gap between items in a row
    int cell_width_= 280;  // per-item width inside a row

    // Optional bounds for clamping and available height decisions
    SDL_Rect work_area_{0,0,0,0};

    bool show_header_ = true;
    bool scroll_enabled_ = true;
    int available_height_override_ = -1;

    std::function<void()> on_close_{};
};
