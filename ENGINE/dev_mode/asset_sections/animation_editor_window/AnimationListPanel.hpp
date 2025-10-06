#pragma once

#include <memory>
#include <vector>

struct SDL_Rect;
struct SDL_Event;
struct SDL_Renderer;

namespace animation_editor {

class AnimationDocument;
class AnimationInspectorPanel;

class AnimationListPanel {
  public:
    AnimationListPanel();

    void set_document(std::shared_ptr<AnimationDocument> document);
    void set_bounds(const SDL_Rect& bounds);

    void update();
    void render(SDL_Renderer* renderer) const;
    bool handle_event(const SDL_Event& e);

  private:
    void rebuild_children();

  private:
    std::shared_ptr<AnimationDocument> document_;
    std::vector<std::unique_ptr<AnimationInspectorPanel>> inspectors_;
    SDL_Rect bounds_{0, 0, 0, 0};
};

}  // namespace animation_editor

