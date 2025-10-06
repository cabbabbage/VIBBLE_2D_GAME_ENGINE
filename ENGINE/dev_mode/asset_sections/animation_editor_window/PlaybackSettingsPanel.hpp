#pragma once

#include <memory>
#include <string>

struct SDL_Rect;
struct SDL_Event;
struct SDL_Renderer;

namespace animation_editor {

class AnimationDocument;

class PlaybackSettingsPanel {
  public:
    PlaybackSettingsPanel();

    void set_document(std::shared_ptr<AnimationDocument> document);
    void set_animation_id(const std::string& animation_id);
    void set_bounds(const SDL_Rect& bounds);

    void update();
    void render(SDL_Renderer* renderer) const;
    bool handle_event(const SDL_Event& e);

  private:
    void sync_from_document();
    void commit_changes();

  private:
    std::shared_ptr<AnimationDocument> document_;
    std::string animation_id_;
    SDL_Rect bounds_{0, 0, 0, 0};
};

}  // namespace animation_editor

