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
    void set_layout_sections(const SDL_Rect& mode_controls_bounds,
                             const SDL_Rect& frame_display_bounds,
                             const SDL_Rect& frame_list_bounds);
    void set_close_callback(CloseCallback callback);
    void set_preview_provider(std::shared_ptr<PreviewProvider> provider);
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

    // Grid control (pixels per grid intersection in the preview)
    void set_grid_pixels(int px);
    int grid_pixels() const { return grid_pixels_px_; }
    void set_grid_resolution_r(int r);
    int grid_resolution_r() const { return grid_resolution_r_; }
    void render_canvas_only(SDL_Renderer* renderer) const;

    int selected_index() const { return selected_index_; }

  private:
    void load_frames_from_document();
    void apply_changes();
    void ensure_children();
    void update_layout();
    void synchronize_selection();
    void mark_dirty();
    void layout_variant_header();
    void render_variant_header(SDL_Renderer* renderer) const;
    bool handle_variant_header_event(const SDL_Event& e);
    void layout_frame_list();
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
    std::vector<MovementVariant> variants_;
    std::vector<VariantTabState> variant_tabs_;
    CloseCallback close_callback_;
    std::vector<MovementFrame> frames_;
    std::vector<SDL_Rect> frame_item_rects_;
    int selected_index_ = 0;
    int active_variant_index_ = 0;
    bool dirty_ = false;
    bool add_button_hovered_ = false;
    bool add_button_pressed_ = false;
    int hovered_frame_index_ = -1;
    int grid_pixels_px_ = 1 << 5; // default 32px
    int grid_resolution_r_ = 5;
    FrameChangedCallback frame_changed_callback_;
};

}

