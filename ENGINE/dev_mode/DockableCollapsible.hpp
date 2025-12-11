#pragma once

#include <SDL.h>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "dm_styles.hpp"
#include "widgets.hpp"

class Input;
class AssetInfo;

class DockableCollapsible {
public:
    using Row = std::vector<Widget*>;
    using Rows = std::vector<Row>;

    explicit DockableCollapsible(const std::string& title, bool floatable = true, int x = 32, int y = 32);
    virtual ~DockableCollapsible();

    static constexpr int kDefaultFloatingContentWidth = 360;

    void set_title(const std::string& title);
    virtual void set_info(const std::shared_ptr<AssetInfo>& info) { info_ = info; }
    virtual void build() {}

    void set_rows(const Rows& rows);

    bool is_visible() const { return visible_; }
    void set_visible(bool v);
    void open();
    void close();
    bool is_expanded() const { return expanded_; }
    void set_expanded(bool e);

    void set_show_header(bool show);
    bool show_header() const { return show_header_; }

    void set_header_button_style(const DMButtonStyle* style);
    void set_header_highlight_color(SDL_Color color);
    void clear_header_highlight_color();

    void set_close_button_enabled(bool enabled);
    void set_close_button_on_left(bool on_left);

    void setLocked(bool locked);
    bool isLocked() const { return locked_; }
    void onLockChanged(std::function<void(bool)> cb);

    void set_scroll_enabled(bool enabled);
    bool scroll_enabled() const { return scroll_enabled_; }

    void set_available_height_override(int height);

    void set_position(int x, int y);
    void set_position_from_layout_manager(int x, int y);
    void set_rect(const SDL_Rect& r);
    SDL_Point position() const { return SDL_Point{rect_.x, rect_.y}; }
    void set_floatable(bool floatable);
    bool is_floatable() const { return floatable_; }
    void set_work_area(const SDL_Rect& area);

    void set_cell_width(int w);
    void set_padding(int p);
    void set_row_gap(int g);
    void set_col_gap(int g);
    void set_visible_height(int h);
    void set_floating_content_width(int w);

    void reset_scroll() const;

    virtual void update(const Input& input, int screen_w, int screen_h);
    virtual bool handle_event(const SDL_Event& e);
    virtual void render(SDL_Renderer* r) const;
    virtual void render_content(SDL_Renderer* r) const {}

    const SDL_Rect& rect() const { return rect_; }
    int height() const { return rect_.h; }
    virtual bool is_point_inside(int x, int y) const;

    void set_on_close(std::function<void()> cb) { on_close_ = std::move(cb); }

    void force_pointer_ready();
    const std::string& title() const { return title_; }
    int embedded_height(int width, int screen_h);
    void render_embedded(SDL_Renderer* renderer, const SDL_Rect& bounds, int screen_w, int screen_h);
    void set_embedded_focus_state(bool focused);
    bool embedded_focus_state() const { return embedded_focus_state_; }
    void set_embedded_interaction_enabled(bool enabled);
    bool embedded_interaction_enabled() const { return embedded_interaction_enabled_; }

protected:

    void set_drag_handle_rect(const SDL_Rect& rect) const { handle_rect_ = rect; }

private:
    void layout(int screen_w, int screen_h) const;
    void invalidate_layout(bool geometry_only = false) const;
    void update_header_button() const;
    void update_lock_button() const;
    int  compute_row_width(int num_cols) const;
    int  available_height(int screen_h) const;
    void clamp_to_bounds(int screen_w, int screen_h) const;
    void clamp_position_only(int screen_w, int screen_h) const;
    void update_geometry_after_move() const;
    void ensure_lock_state_initialized() const;
    void ensure_lock_button() const;
    const std::string& lock_settings_key() const;
    bool should_show_lock_button() const;
    void apply_lock_state(bool locked, bool allow_auto_collapse, bool persist) const;
    void render_locked_children_overlay(SDL_Renderer* r) const;
    void log_locked_mutation(std::string_view method) const;
    void set_position_internal(int x, int y, bool from_layout_manager);
    void update_layout_manager_registration();
    void notify_layout_manager_geometry_changed() const;
    void notify_layout_manager_content_changed() const;
    void block_pointer_for(Uint32 ms) const;
    bool pointer_block_active() const;

protected:
    virtual void layout();
    virtual void layout_custom_content(int screen_w, int screen_h) const {}
    virtual std::string_view lock_settings_namespace() const { return {}; }
    virtual std::string_view lock_settings_id() const { return {}; }

protected:
    std::string title_;
    mutable std::unique_ptr<DMButton> header_btn_;
    mutable std::unique_ptr<DMButton> close_btn_;
    mutable std::unique_ptr<DMButton> lock_btn_;
    const DMButtonStyle* header_button_style_ = &DMStyles::HeaderButton();
    std::optional<SDL_Color> header_highlight_override_{};
    mutable SDL_Rect rect_{32,32,260,DMButton::height()+8};
    mutable SDL_Rect header_rect_{0,0,0,0};
    mutable SDL_Rect handle_rect_{0,0,0,0};
    mutable SDL_Rect close_rect_{0,0,0,0};
    mutable SDL_Rect lock_rect_{0,0,0,0};
    mutable SDL_Rect body_viewport_{0,0,0,0};

    Rows rows_;
    mutable std::vector<int> row_heights_;
    mutable int content_height_ = 0;
    mutable int widest_row_w_ = 0;
    mutable int body_viewport_h_ = 0;
    int visible_height_ = 400;

    bool visible_ = true;
    bool expanded_ = false;
    bool floatable_ = true;
    bool close_button_enabled_ = false;
    bool close_button_on_left_ = false;
    bool dragging_ = false;
    bool header_dragging_via_button_ = false;
    bool drag_exceeded_threshold_ = false;
    SDL_Point drag_offset_{0,0};
    SDL_Point drag_start_pointer_{0,0};
    mutable Uint32 pointer_block_until_ms_ = 0;
    mutable int scroll_ = 0;
    mutable int max_scroll_ = 0;
    std::shared_ptr<AssetInfo> info_{};

    mutable bool locked_ = false;
    mutable bool lock_state_initialized_ = false;
    mutable std::string lock_settings_key_cache_{};
    mutable bool lock_settings_key_cached_ = false;
    std::vector<std::function<void(bool)>> on_lock_changed_{};

    int padding_   = 10;
    int row_gap_   = 8;
    int col_gap_   = 12;
    int cell_width_= 280;
    int floating_content_width_ = kDefaultFloatingContentWidth;

    SDL_Rect work_area_{0,0,0,0};

    bool show_header_ = true;
    bool scroll_enabled_ = true;
    int available_height_override_ = -1;

    std::function<void()> on_close_{};

    mutable int last_screen_w_ = 0;
    mutable int last_screen_h_ = 0;
    mutable std::unordered_set<std::string> locked_mutation_warnings_{};
    mutable bool needs_layout_ = true;
    mutable bool needs_geometry_ = true;
    mutable bool layout_initialized_ = false;

    bool registered_with_layout_manager_ = false;
    bool embedded_focus_state_ = false;
    bool embedded_interaction_enabled_ = true;
    bool rendering_embedded_ = false;

    struct EmbeddedSnapshot {
        SDL_Rect rect;
        bool visible;
        bool expanded;
        bool floatable;
        bool scroll_enabled;
        int visible_height;
        int available_height_override;
        int last_screen_w;
        int last_screen_h;
};

    void capture_snapshot(EmbeddedSnapshot& out) const;
    void apply_embedded_bounds(const SDL_Rect& bounds, int screen_w, int screen_h);
    void restore_snapshot(const EmbeddedSnapshot& snapshot);
};
