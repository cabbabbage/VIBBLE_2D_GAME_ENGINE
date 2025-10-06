#include "TotalsPanel.hpp"

#include "MovementCanvas.hpp"

namespace animation_editor {

TotalsPanel::TotalsPanel() = default;

void TotalsPanel::set_bounds(const SDL_Rect& bounds) {
    bounds_ = bounds;
    // TODO: Layout totals labels and navigation controls.
}

void TotalsPanel::set_frames(const std::vector<MovementFrame>& frames) {
    frames_ = frames;
    // TODO: Update totals and frame navigation metadata.
    recalculate_totals();
}

void TotalsPanel::update() {
    // TODO: Update UI state for navigation buttons and totals display.
}

void TotalsPanel::render(SDL_Renderer* renderer) const {
    (void)renderer;
    // TODO: Draw total delta readouts and frame navigation buttons.
}

bool TotalsPanel::handle_event(const SDL_Event& e) {
    (void)e;
    // TODO: Handle next/previous frame controls and totals redistribution actions.
    return false;
}

void TotalsPanel::recalculate_totals() {
    // TODO: Sum movement frames to compute total dx/dy values.
    total_dx_ = 0.0f;
    total_dy_ = 0.0f;
}

}  // namespace animation_editor

