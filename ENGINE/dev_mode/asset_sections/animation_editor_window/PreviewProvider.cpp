#include "PreviewProvider.hpp"

#include "AnimationDocument.hpp"

namespace animation_editor {

PreviewProvider::PreviewProvider() = default;

void PreviewProvider::set_document(std::shared_ptr<AnimationDocument> document) {
    document_ = std::move(document);
    // TODO: Drop cached textures when document changes.
    invalidate_all();
}

SDL_Texture* PreviewProvider::get_preview_texture(SDL_Renderer* renderer, const std::string& animation_id) {
    (void)renderer;
    (void)animation_id;
    // TODO: Render or fetch cached preview texture for the first frame respecting flips.
    return nullptr;
}

void PreviewProvider::invalidate(const std::string& animation_id) {
    (void)animation_id;
    // TODO: Remove cached texture for specified animation id.
}

void PreviewProvider::invalidate_all() {
    // TODO: Clear all cached textures when document reloads or resources change.
}

}  // namespace animation_editor

