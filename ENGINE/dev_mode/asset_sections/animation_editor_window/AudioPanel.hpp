#pragma once

#include <memory>
#include <string>

struct SDL_Rect;
struct SDL_Event;
struct SDL_Renderer;

namespace animation_editor {

class AnimationDocument;
class AudioImporter;

class AudioPanel {
  public:
    AudioPanel();

    void set_document(std::shared_ptr<AnimationDocument> document);
    void set_animation_id(const std::string& animation_id);
    void set_bounds(const SDL_Rect& bounds);
    void set_importer(std::shared_ptr<AudioImporter> importer);

    void update();
    void render(SDL_Renderer* renderer) const;
    bool handle_event(const SDL_Event& e);

  private:
    void attach_audio();
    void replace_audio();
    void remove_audio();
    void preview_audio();

  private:
    std::shared_ptr<AnimationDocument> document_;
    std::shared_ptr<AudioImporter> importer_;
    std::string animation_id_;
    SDL_Rect bounds_{0, 0, 0, 0};
    float volume_ = 1.0f;
    bool effects_enabled_ = false;
};

}  // namespace animation_editor

