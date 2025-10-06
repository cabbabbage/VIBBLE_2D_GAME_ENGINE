#include "FramePropertiesPanel.hpp"

#include "MovementCanvas.hpp"

namespace animation_editor {

FramePropertiesPanel::FramePropertiesPanel() = default;

void FramePropertiesPanel::set_bounds(const SDL_Rect& bounds) {
    bounds_ = bounds;
    // TODO: Layout resort-z toggle, tint picker, and metadata fields.
}

void FramePropertiesPanel::set_frames(std::vector<MovementFrame>* frames) {
    frames_ = frames;
    // TODO: Refresh UI fields when frames pointer changes.
}

void FramePropertiesPanel::set_selected_index(int* selected_index) {
    selected_index_ = selected_index;
    // TODO: Sync selected frame metadata with UI controls.
    sync_from_selected();
}

void FramePropertiesPanel::update() {
    // TODO: Update interactive controls and respond to selection changes.
}

void FramePropertiesPanel::render(SDL_Renderer* renderer) const {
    (void)renderer;
    // TODO: Draw property controls for the currently selected frame.
}

bool FramePropertiesPanel::handle_event(const SDL_Event& e) {
    (void)e;
    // TODO: Process UI interactions and write changes back to selected frame.
    return false;
}

void FramePropertiesPanel::sync_from_selected() {
    // TODO: Load tint/resort-z data from the selected frame into local state.
}

void FramePropertiesPanel::apply_to_selected() {
    // TODO: Apply pending UI changes to the selected frame object.
}

}  // namespace animation_editor

