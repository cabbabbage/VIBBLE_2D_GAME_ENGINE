#include "PlaybackSettingsPanel.hpp"

#include "AnimationDocument.hpp"

namespace animation_editor {

PlaybackSettingsPanel::PlaybackSettingsPanel() = default;

void PlaybackSettingsPanel::set_document(std::shared_ptr<AnimationDocument> document) {
    document_ = std::move(document);
    // TODO: Refresh playback options from document state.
}

void PlaybackSettingsPanel::set_animation_id(const std::string& animation_id) {
    animation_id_ = animation_id;
    // TODO: Sync UI controls for the requested animation.
    sync_from_document();
}

void PlaybackSettingsPanel::set_bounds(const SDL_Rect& bounds) {
    bounds_ = bounds;
    // TODO: Layout toggles, sliders, and numeric inputs for playback settings.
}

void PlaybackSettingsPanel::update() {
    // TODO: Update spinner animations or validation states.
}

void PlaybackSettingsPanel::render(SDL_Renderer* renderer) const {
    (void)renderer;
    // TODO: Draw flip, reverse, lock, loop, random start, and speed controls.
}

bool PlaybackSettingsPanel::handle_event(const SDL_Event& e) {
    (void)e;
    // TODO: Respond to toggle clicks and numeric adjustments, then persist changes.
    return false;
}

void PlaybackSettingsPanel::sync_from_document() {
    // TODO: Pull playback fields from the animation payload into local state.
}

void PlaybackSettingsPanel::commit_changes() {
    // TODO: Push local playback configuration back into the document.
}

}  // namespace animation_editor

