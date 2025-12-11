#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <SDL.h>

#include "MovementCanvas.hpp"
#include "TotalsPanel.hpp"
#include "FramePropertiesPanel.hpp"

class DMButton;

namespace animation_editor {

class AnimationDocument;
class PreviewProvider;

using DMButton = ::DMButton;
class FrameMovementEditor {
  public:
    using CloseCallback = std::function<void()>;
    using FrameChangedCallback = std::function<void(int)>;

    FrameMovementEditor();

    void set_document(std::shared_ptr<AnimationDocument> document);
    void set_animation_id(const std::string& animation_id);
    void set_layout_sections(const SDL_Rect& mode_controls_bounds, const SDL_Rect& frame_display_bounds, const SDL_Rect& frame_list_bounds);
    void set_close_callback(CloseCallback callback);
    void set_preview_provider(std::shared_ptr<PreviewProvider> provider);
    void set_frame_list_override(int count, const std::string& animation_id, bool preserve_selection);
    void set_frame_changed_callback(FrameChangedCallback callback) { frame_changed_callback_ = std::move(callback); }

    void update();
    void render(SDL_Renderer* renderer) const;
    bool handle_event(const SDL_Event& e);
    void render_frame_list(SDL_Renderer* renderer) const;
    bool handle_frame_list_event(const SDL_Event& e);

    bool can_select_previous_frame() const;
    bool can_select_next_frame() const;
    void select_previous_frame();
    void select_next_frame();

    void render_canvas_only(SDL_Renderer* renderer) const;

    int selected_index() const { return selected_index_; }
    MovementCanvas* canvas() { return canvas_.get(); }
    const MovementCanvas* canvas() const { return canvas_.get(); }

    void set_show_animation(bool show);
    bool show_animation() const { return show_animation_; }
    void set_smoothing_enabled(bool enabled);
    void set_curve_enabled(bool enabled);
    void apply_smoothing();
    std::pair<int,int> total_displacement() const;
    void set_total_displacement(int dx, int dy);
    void set_grid_snap_resolution(int r) { if (canvas_) canvas_->set_snap_resolution(r); }

  private:
    void load_frames_from_document();
    void apply_changes();
    void ensure_children();
    void update_layout();
    void synchronize_selection();
    void ensure_selection_visible();
    void mark_dirty();
    void layout_variant_header();
    void render_variant_header(SDL_Renderer* renderer) const;
    bool handle_variant_header_event(const SDL_Event& e);
    void layout_frame_list();
    int view_frame_count() const;
    int map_view_to_actual(int view_index) const;
    int view_index_for_actual(int actual_index) const;
    int clamp_view_index(int index) const;
    void sync_view_selection_from_actual();
    void set_active_variant(int index, bool preserve_view);
    void update_child_frames(bool preserve_view);
    void sync_active_variant_frames();
    void add_new_variant();
    void delete_variant(int index);
    std::string generate_variant_name() const;
    void smooth_frames();

  private:
    struct MovementVariant {
        std::string name;
        std::vector<MovementFrame> frames;
        bool primary = false;
};

    struct VariantTabState {
        SDL_Rect rect{0, 0, 0, 0};
        SDL_Rect close_rect{0, 0, 0, 0};
        bool close_visible = false;
        bool hovered = false;
        bool pressed = false;
        bool close_hovered = false;
        bool close_pressed = false;
};

    std::shared_ptr<AnimationDocument> document_;
    std::unique_ptr<MovementCanvas> canvas_;
    std::unique_ptr<TotalsPanel> totals_panel_;
    std::unique_ptr<FramePropertiesPanel> properties_panel_;

    std::unique_ptr<DMButton> smooth_button_;
    std::unique_ptr<DMButton> show_anim_button_;
    std::shared_ptr<PreviewProvider> preview_provider_;
    std::string animation_id_;
    SDL_Rect mode_controls_rect_{0, 0, 0, 0};
    SDL_Rect frame_display_rect_{0, 0, 0, 0};
    SDL_Rect frame_list_rect_{0, 0, 0, 0};
    SDL_Rect header_rect_{0, 0, 0, 0};
    SDL_Rect totals_rect_{0, 0, 0, 0};
    SDL_Rect properties_rect_{0, 0, 0, 0};
    SDL_Rect add_button_rect_{0, 0, 0, 0};
    SDL_Rect smooth_button_rect_{0, 0, 0, 0};
    SDL_Rect show_anim_button_rect_{0, 0, 0, 0};
    std::vector<MovementVariant> variants_;
    std::vector<VariantTabState> variant_tabs_;
    CloseCallback close_callback_;
    std::vector<MovementFrame> frames_;
    std::vector<SDL_Rect> frame_item_rects_;
    int frame_list_override_count_ = -1;
    std::string frame_list_override_animation_id_;
    int display_selected_index_ = 0;

    int hscroll_offset_px_ = 0;
    int hscroll_content_px_ = 0;
    SDL_Rect hscroll_track_rect_{0,0,0,0};
    SDL_Rect hscroll_knob_rect_{0,0,0,0};
    bool hscroll_dragging_ = false;
    int  hscroll_drag_dx_ = 0;

    SDL_Rect fl_prev_button_rect_{0,0,0,0};
    SDL_Rect fl_next_button_rect_{0,0,0,0};
    bool fl_prev_hovered_ = false;
    bool fl_next_hovered_ = false;
    bool fl_prev_pressed_ = false;
    bool fl_next_pressed_ = false;
    int selected_index_ = 0;
    int active_variant_index_ = 0;
    bool dirty_ = false;
    bool add_button_hovered_ = false;
    bool add_button_pressed_ = false;
    int hovered_frame_index_ = -1;
    bool show_animation_ = true;
    bool smoothing_enabled_ = false;
    bool curve_enabled_ = false;
    FrameChangedCallback frame_changed_callback_;
};

}

