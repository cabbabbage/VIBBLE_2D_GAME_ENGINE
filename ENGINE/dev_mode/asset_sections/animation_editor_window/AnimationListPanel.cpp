#include "AnimationListPanel.hpp"

#include "AnimationDocument.hpp"
#include "AnimationInspectorPanel.hpp"

namespace animation_editor {

AnimationListPanel::AnimationListPanel() = default;

void AnimationListPanel::set_document(std::shared_ptr<AnimationDocument> document) {
    document_ = std::move(document);
    // TODO: Rebuild inspector cards from document animations.
    rebuild_children();
}

void AnimationListPanel::set_bounds(const SDL_Rect& bounds) {
    bounds_ = bounds;
    // TODO: Reflow inspector widgets within the provided bounds.
}

void AnimationListPanel::update() {
    // TODO: Update hover/selection state for all inspector panels.
}

void AnimationListPanel::render(SDL_Renderer* renderer) const {
    (void)renderer;
    // TODO: Draw scrollable list of animation inspector cards.
}

bool AnimationListPanel::handle_event(const SDL_Event& e) {
    (void)e;
    // TODO: Forward input events to child inspectors and manage scrolling.
    return false;
}

void AnimationListPanel::rebuild_children() {
    // TODO: Create AnimationInspectorPanel instances for each animation in the document.
    inspectors_.clear();
}

}  // namespace animation_editor

