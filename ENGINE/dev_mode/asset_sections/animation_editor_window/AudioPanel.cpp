#include "AudioPanel.hpp"

#include "AnimationDocument.hpp"
#include "AudioImporter.hpp"

namespace animation_editor {

AudioPanel::AudioPanel() = default;

void AudioPanel::set_document(std::shared_ptr<AnimationDocument> document) {
    document_ = std::move(document);
    // TODO: Sync audio metadata from document payload.
}

void AudioPanel::set_animation_id(const std::string& animation_id) {
    animation_id_ = animation_id;
    // TODO: Load audio attachment info for the specified animation.
}

void AudioPanel::set_bounds(const SDL_Rect& bounds) {
    bounds_ = bounds;
    // TODO: Layout audio controls, volume slider, and preview button.
}

void AudioPanel::set_importer(std::shared_ptr<AudioImporter> importer) {
    importer_ = std::move(importer);
    // TODO: Use importer for audio attach/replace workflows.
}

void AudioPanel::update() {
    // TODO: Update playback preview state and respond to importer progress.
}

void AudioPanel::render(SDL_Renderer* renderer) const {
    (void)renderer;
    // TODO: Draw audio attachment UI and metadata.
}

bool AudioPanel::handle_event(const SDL_Event& e) {
    (void)e;
    // TODO: Handle attach/replace/remove/preview interactions and persist volume/effects toggles.
    return false;
}

void AudioPanel::attach_audio() {
    // TODO: Open file picker and attach selected audio via AudioImporter.
}

void AudioPanel::replace_audio() {
    // TODO: Replace current audio attachment, updating document and asset references.
}

void AudioPanel::remove_audio() {
    // TODO: Detach audio from animation and update document payload.
}

void AudioPanel::preview_audio() {
    // TODO: Play or stop audio preview using importer playback facilities.
}

}  // namespace animation_editor

