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

class FrameEditor {
  public:
    enum class Mode {
        Movement = 0,
        Children = 1,
        Attacking = 2,
    };

    using CloseCallback = std::function<void()>;

    FrameEditor();
    ~FrameEditor();

    void set_document(std::shared_ptr<AnimationDocument> document);
    void set_animation_id(const std::string& animation_id);
    void set_bounds(const SDL_Rect& bounds);
    void set_close_callback(CloseCallback callback);

    void update();
    void render(SDL_Renderer* renderer) const;
    bool handle_event(const SDL_Event& e);

  private:
    void ensure_children();
    void update_layout();
    void set_mode(Mode mode);
    void update_button_styles() const;
    SDL_Rect content_rect() const;

  private:
    std::shared_ptr<AnimationDocument> document_;
    std::unique_ptr<FrameMovementEditor> movement_editor_;
    std::array<std::unique_ptr<DMButton>, 3> mode_buttons_;
    SDL_Rect bounds_{0, 0, 0, 0};
    SDL_Rect tabs_rect_{0, 0, 0, 0};
    std::string animation_id_;
    CloseCallback close_callback_;
    Mode active_mode_ = Mode::Movement;
};

}  // namespace animation_editor

