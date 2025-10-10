#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <SDL.h>

#include "MovementCanvas.hpp"
#include "TotalsPanel.hpp"
#include "FramePropertiesPanel.hpp"

namespace animation_editor {

class AnimationDocument;
class FrameMovementEditor {
  public:
    using CloseCallback = std::function<void()>;

    FrameMovementEditor();

    void set_document(std::shared_ptr<AnimationDocument> document);
    void set_animation_id(const std::string& animation_id);
    void set_bounds(const SDL_Rect& bounds);
    void set_close_callback(CloseCallback callback);

    void update();
    void render(SDL_Renderer* renderer) const;
    bool handle_event(const SDL_Event& e);

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
    void set_active_variant(int index, bool preserve_view);
    void update_child_frames(bool preserve_view);
    void sync_active_variant_frames();
    void add_new_variant();
    void delete_variant(int index);
    std::string generate_variant_name() const;

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
    std::string animation_id_;
    SDL_Rect bounds_{0, 0, 0, 0};
    SDL_Rect header_rect_{0, 0, 0, 0};
    SDL_Rect add_button_rect_{0, 0, 0, 0};
    std::vector<MovementVariant> variants_;
    std::vector<VariantTabState> variant_tabs_;
    CloseCallback close_callback_;
    std::vector<MovementFrame> frames_;
    int selected_index_ = 0;
    int active_variant_index_ = 0;
    bool dirty_ = false;
    bool add_button_hovered_ = false;
    bool add_button_pressed_ = false;
};

}

