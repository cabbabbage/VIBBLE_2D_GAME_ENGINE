#include "OnEndSelector.hpp"

#include "AnimationDocument.hpp"

namespace animation_editor {

OnEndSelector::OnEndSelector() = default;

void OnEndSelector::set_document(std::shared_ptr<AnimationDocument> document) {
    document_ = std::move(document);
    // TODO: Refresh transition options from document state.
    rebuild_options();
}

void OnEndSelector::set_animation_id(const std::string& animation_id) {
    animation_id_ = animation_id;
    // TODO: Sync dropdown selection with animation payload.
}

void OnEndSelector::set_bounds(const SDL_Rect& bounds) {
    bounds_ = bounds;
    // TODO: Position dropdown widget inside inspector layout.
}

void OnEndSelector::update() {
    // TODO: Update hover/selection state if needed.
}

void OnEndSelector::render(SDL_Renderer* renderer) const {
    (void)renderer;
    // TODO: Draw dropdown control listing on_end options.
}

bool OnEndSelector::handle_event(const SDL_Event& e) {
    (void)e;
    // TODO: Handle option selection and commit to document.
    return false;
}

void OnEndSelector::rebuild_options() {
    // TODO: Populate options list with default transitions plus animation ids.
    options_.clear();
}

}  // namespace animation_editor

