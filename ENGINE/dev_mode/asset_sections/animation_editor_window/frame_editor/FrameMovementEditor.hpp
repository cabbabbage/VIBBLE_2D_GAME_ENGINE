#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

struct SDL_Rect;
struct SDL_Event;
struct SDL_Renderer;

namespace animation_editor {

class AnimationDocument;
class MovementCanvas;
class TotalsPanel;
class FramePropertiesPanel;

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

  private:
    std::shared_ptr<AnimationDocument> document_;
    std::unique_ptr<MovementCanvas> canvas_;
    std::unique_ptr<TotalsPanel> totals_panel_;
    std::unique_ptr<FramePropertiesPanel> properties_panel_;
    std::string animation_id_;
    SDL_Rect bounds_{0, 0, 0, 0};
    CloseCallback close_callback_;
};

}  // namespace animation_editor

