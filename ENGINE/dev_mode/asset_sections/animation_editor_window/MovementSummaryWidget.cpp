#include "MovementSummaryWidget.hpp"

#include "AnimationDocument.hpp"

namespace animation_editor {

MovementSummaryWidget::MovementSummaryWidget() = default;

void MovementSummaryWidget::set_document(std::shared_ptr<AnimationDocument> document) {
    document_ = std::move(document);
    // TODO: Attach to document change notifications for totals updates.
}

void MovementSummaryWidget::set_animation_id(const std::string& animation_id) {
    animation_id_ = animation_id;
    // TODO: Fetch movement totals from animation payload and update display.
    refresh_totals();
}

void MovementSummaryWidget::set_bounds(const SDL_Rect& bounds) {
    bounds_ = bounds;
    // TODO: Position totals readout and edit button within the inspector card.
}

void MovementSummaryWidget::set_edit_callback(EditCallback callback) {
    edit_callback_ = std::move(callback);
    // TODO: Trigger frame editor when user clicks edit button.
}

void MovementSummaryWidget::update() {
    // TODO: Animate totals changes or respond to document updates.
}

void MovementSummaryWidget::render(SDL_Renderer* renderer) const {
    (void)renderer;
    // TODO: Draw totals text and edit movement button.
}

bool MovementSummaryWidget::handle_event(const SDL_Event& e) {
    (void)e;
    // TODO: Detect button presses and invoke edit callback with animation id.
    return false;
}

void MovementSummaryWidget::refresh_totals() {
    // TODO: Aggregate per-frame deltas to compute total movement preview.
}

}  // namespace animation_editor

