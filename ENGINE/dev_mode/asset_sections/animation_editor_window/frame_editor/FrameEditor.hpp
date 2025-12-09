#pragma once

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include <SDL.h>

class DMButton;

namespace animation_editor {

class AnimationDocument;
class FrameMovementEditor;
class FrameChildrenEditor;
class PreviewProvider;
class FrameToolsPanel;

using DMButton = ::DMButton;

class FrameEditor {
  public:
    enum class Mode {
        Movement = 0,
        StaticChildren = 1,
        AsyncChildren = 2,
        AttackGeometry = 3,
        HitGeometry = 4,
};

    using CloseCallback = std::function<void()>;
    using FrameChangedCallback = std::function<void(int)>;

    FrameEditor();
    ~FrameEditor();

    void set_document(std::shared_ptr<AnimationDocument> document);
    void set_animation_id(const std::string& animation_id);
    void set_bounds(const SDL_Rect& bounds);
    void set_close_callback(CloseCallback callback);
    void set_preview_provider(std::shared_ptr<PreviewProvider> provider);
    void set_frame_changed_callback(FrameChangedCallback callback);
    void set_grid_snap_resolution(int r);
    int selected_index() const;

    void update();
    void render(SDL_Renderer* renderer) const;
    bool handle_event(const SDL_Event& e);

  private:
    void ensure_children();
    void update_layout();
    void set_mode(Mode mode);
    void update_button_styles() const;
    void update_navigation_styles() const;

  private:
    std::shared_ptr<AnimationDocument> document_;
    std::unique_ptr<FrameMovementEditor> movement_editor_;
    std::unique_ptr<FrameChildrenEditor> children_editor_;
    std::unique_ptr<FrameToolsPanel> tools_panel_;
    std::shared_ptr<PreviewProvider> preview_provider_;
    std::array<std::unique_ptr<DMButton>, 4> mode_buttons_;
    std::unique_ptr<DMButton> prev_frame_button_;
    std::unique_ptr<DMButton> next_frame_button_;
    SDL_Rect bounds_{0, 0, 0, 0};
    SDL_Rect header_rect_{0, 0, 0, 0};
    SDL_Rect mode_controls_rect_{0, 0, 0, 0};
    SDL_Rect frame_display_rect_{0, 0, 0, 0};
    SDL_Rect frame_list_rect_{0, 0, 0, 0};
    SDL_Rect tools_panel_rect_{0, 0, 0, 0};
    SDL_Rect prev_button_rect_{0, 0, 0, 0};
    SDL_Rect next_button_rect_{0, 0, 0, 0};
    std::string animation_id_;
    CloseCallback close_callback_;
    FrameChangedCallback frame_changed_callback_;
    Mode active_mode_ = Mode::Movement;
    bool tools_panel_follow_layout_ = true;

    SDL_Rect tools_panel_hit_rect() const;
};

}
