#include "MovementCanvas.hpp"

namespace animation_editor {

MovementCanvas::MovementCanvas() = default;

void MovementCanvas::set_bounds(const SDL_Rect& bounds) {
    bounds_ = bounds;
    // TODO: Recalculate zoom-to-fit metrics based on new bounds.
}

void MovementCanvas::set_frames(const std::vector<MovementFrame>& frames) {
    frames_ = frames;
    // TODO: Rebuild cached path geometry for rendering.
}

void MovementCanvas::update() {
    // TODO: Update hover states and handle keyboard shortcuts for frame editing.
}

void MovementCanvas::render(SDL_Renderer* renderer) const {
    (void)renderer;
    // TODO: Draw axes, path segments, and frame markers using renderer primitives.
}

bool MovementCanvas::handle_event(const SDL_Event& e) {
    (void)e;
    // TODO: Process mouse drags for movement editing and zoom/pan gestures.
    return false;
}

void MovementCanvas::pan_view(float delta_x, float delta_y) {
    (void)delta_x;
    (void)delta_y;
    // TODO: Adjust camera offset by the specified delta.
}

void MovementCanvas::apply_zoom(float scale_delta) {
    (void)scale_delta;
    // TODO: Modify zoom level while keeping cursor position anchored.
}

void MovementCanvas::update_selection_from_mouse() {
    // TODO: Determine which frame is under the cursor and update selection state.
}

}  // namespace animation_editor

