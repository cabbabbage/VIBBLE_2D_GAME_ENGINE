#include "AnimationInspectorPanel.hpp"

#include "AnimationDocument.hpp"
#include "AudioPanel.hpp"
#include "MovementSummaryWidget.hpp"
#include "OnEndSelector.hpp"
#include "PlaybackSettingsPanel.hpp"
#include "PreviewProvider.hpp"
#include "SourceConfigPanel.hpp"

namespace animation_editor {

AnimationInspectorPanel::AnimationInspectorPanel() = default;

void AnimationInspectorPanel::set_document(std::shared_ptr<AnimationDocument> document) {
    document_ = std::move(document);
    // TODO: Bind document change listeners for inspector widgets.
}

void AnimationInspectorPanel::set_animation_id(const std::string& animation_id) {
    animation_id_ = animation_id;
    // TODO: Refresh inspector fields to reflect the new animation payload.
    rebuild_widgets();
}

void AnimationInspectorPanel::set_bounds(const SDL_Rect& bounds) {
    bounds_ = bounds;
    // TODO: Layout child widgets within the inspector card.
}

void AnimationInspectorPanel::set_preview_provider(std::shared_ptr<PreviewProvider> provider) {
    preview_provider_ = std::move(provider);
    // TODO: Request thumbnail updates when animation source changes.
}

void AnimationInspectorPanel::update() {
    // TODO: Update internal widgets (source config, playback, etc.).
}

void AnimationInspectorPanel::render(SDL_Renderer* renderer) const {
    (void)renderer;
    // TODO: Draw card background, header, preview, and embedded panels.
}

bool AnimationInspectorPanel::handle_event(const SDL_Event& e) {
    (void)e;
    // TODO: Dispatch events to rename field, buttons, and child panels.
    return false;
}

void AnimationInspectorPanel::rebuild_widgets() {
    // TODO: Construct or refresh child panels with current animation data.
}

void AnimationInspectorPanel::refresh_totals() {
    // TODO: Update movement totals display based on animation payload.
}

}  // namespace animation_editor

