#pragma once

#include <memory>
#include <optional>
#include <string>

struct SDL_Renderer;
struct SDL_Texture;

namespace animation_editor {

class AnimationDocument;

class PreviewProvider {
  public:
    PreviewProvider();

    void set_document(std::shared_ptr<AnimationDocument> document);

    SDL_Texture* get_preview_texture(SDL_Renderer* renderer, const std::string& animation_id);
    void invalidate(const std::string& animation_id);
    void invalidate_all();

  private:
    // TODO: Cache generated textures per animation id.
    std::shared_ptr<AnimationDocument> document_;
};

}  // namespace animation_editor

