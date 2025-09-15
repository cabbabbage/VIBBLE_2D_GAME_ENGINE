#pragma once

#include <SDL.h>
#include <memory>
#include <string>
#include <vector>

#include "dm_styles.hpp"
#include "widgets.hpp"

class Input; // fwd

// Draggable, collapsible floating container for dev-mode panels.
// - Rows: std::vector<std::vector<Widget*>>; each inner vector is a row
// - Each row uses even column widths; row height = tallest widget in row
// - Auto sizes width to the widest row
// - Scrolls when content exceeds available height
// - Header shows title + arrow and includes a drag handle area
// - Designed to render behind AssetInfoUI (call render() before AssetInfoUI)
class FloatingCollapsible {
public:
    using Row = std::vector<Widget*>;
    using Rows = std::vector<Row>;

    explicit FloatingCollapsible(const std::string& title, int x = 32, int y = 32);
    ~FloatingCollapsible();

    // Content rows are non-owning Widget* wrappers (see ui_widget.hpp adapters)
    void set_rows(const Rows& rows);

    // Visibility and collapse state
    bool is_visible() const { return visible_; }
    void set_visible(bool v) { visible_ = v; }
    bool is_expanded() const { return expanded_; }
    void set_expanded(bool e);

    // Position and bounds (work area is used for clamping and available height)
    void set_position(int x, int y);
    SDL_Point position() const { return SDL_Point{rect_.x, rect_.y}; }
    void set_work_area(const SDL_Rect& area); // e.g., full screen or a sub-rect

    // Layout configuration
    void set_cell_width(int w) { cell_width_ = std::max(40, w); }
    void set_padding(int p)    { padding_ = std::max(0, p); }
    void set_row_gap(int g)    { row_gap_ = std::max(0, g); }
    void set_col_gap(int g)    { col_gap_ = std::max(0, g); }

    // Event/update/render
    void update(const Input& input, int screen_w, int screen_h);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r) const;

    // Rect of the whole floating panel
    const SDL_Rect& rect() const { return rect_; }

private:
    void layout(int screen_w, int screen_h) const;
    void update_header_button() const;
    int  compute_row_width(int num_cols) const;
    int  available_height(int screen_h) const;
    void clamp_to_bounds(int screen_w, int screen_h) const;

private:
    std::string title_;
    mutable std::unique_ptr<DMButton> header_btn_;
    mutable SDL_Rect rect_{32,32,260,DMButton::height()+8};
    mutable SDL_Rect header_rect_{0,0,0,0};
    mutable SDL_Rect handle_rect_{0,0,0,0};
    mutable SDL_Rect body_viewport_{0,0,0,0};

    Rows rows_;
    mutable std::vector<int> row_heights_;
    mutable int content_height_ = 0;  // full content height (unclipped)
    mutable int widest_row_w_ = 0;    // computed panel width
    mutable int body_viewport_h_ = 0; // visible body height (clipped)

    // Interaction
    bool visible_ = true;
    bool expanded_ = false;
    bool dragging_ = false;
    SDL_Point drag_offset_{0,0};
    mutable int scroll_ = 0;
    mutable int max_scroll_ = 0;

    // Layout config
    int padding_   = 10;   // outer padding
    int row_gap_   = 8;    // vertical gap between rows
    int col_gap_   = 12;   // horizontal gap between items in a row
    int cell_width_= 220;  // per-item width inside a row

    // Optional bounds for clamping and available height decisions
    SDL_Rect work_area_{0,0,0,0};
};
