#pragma once

#include <memory>
#include <string>
#include <vector>

struct SDL_Rect;
struct SDL_Event;
struct SDL_Renderer;

namespace animation_editor {

class AnimationDocument;
class AnimationInspectorPanel;
class PreviewProvider;

class AnimationListPanel {
  public:
    AnimationListPanel();

    void set_document(std::shared_ptr<AnimationDocument> document);
    void set_bounds(const SDL_Rect& bounds);
    void set_preview_provider(std::shared_ptr<PreviewProvider> provider);

    void update();
    void render(SDL_Renderer* renderer) const;
    bool handle_event(const SDL_Event& e);

  private:
    void rebuild_children();
    void layout_inspectors();
    void clamp_scroll();

  private:
    std::shared_ptr<AnimationDocument> document_;
    std::vector<std::unique_ptr<AnimationInspectorPanel>> inspectors_;
    std::vector<SDL_Rect> inspector_bounds_;
    std::vector<std::string> cached_animation_ids_;
    std::shared_ptr<PreviewProvider> preview_provider_;
    SDL_Rect bounds_{0, 0, 0, 0};
    int scroll_offset_ = 0;
    int content_height_ = 0;
    bool layout_dirty_ = true;
};

}  // namespace animation_editor

