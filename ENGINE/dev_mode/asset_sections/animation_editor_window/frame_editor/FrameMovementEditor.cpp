#include "FrameMovementEditor.hpp"

#include "AnimationDocument.hpp"
#include "FramePropertiesPanel.hpp"
#include "MovementCanvas.hpp"
#include "TotalsPanel.hpp"

namespace animation_editor {

FrameMovementEditor::FrameMovementEditor() = default;

void FrameMovementEditor::set_document(std::shared_ptr<AnimationDocument> document) {
    document_ = std::move(document);
    // TODO: Propagate document reference to child widgets for data access.
}

void FrameMovementEditor::set_animation_id(const std::string& animation_id) {
    animation_id_ = animation_id;
    // TODO: Load movement frames from document into editor state.
    load_frames_from_document();
}

void FrameMovementEditor::set_bounds(const SDL_Rect& bounds) {
    bounds_ = bounds;
    // TODO: Arrange canvas and side panels within the frame editor viewport.
}

void FrameMovementEditor::set_close_callback(CloseCallback callback) {
    close_callback_ = std::move(callback);
    // TODO: Invoke close callback when user exits frame editor.
}

void FrameMovementEditor::update() {
    // TODO: Update canvas interactions and panel state changes.
}

void FrameMovementEditor::render(SDL_Renderer* renderer) const {
    (void)renderer;
    // TODO: Draw movement path canvas, totals, and frame property controls.
}

bool FrameMovementEditor::handle_event(const SDL_Event& e) {
    (void)e;
    // TODO: Route input events to canvas and panels, handle accept/cancel actions.
    return false;
}

void FrameMovementEditor::load_frames_from_document() {
    // TODO: Fetch per-frame movement deltas and metadata from the document.
}

void FrameMovementEditor::apply_changes() {
    // TODO: Push edited movement data back to the document and totals widget.
}

}  // namespace animation_editor

