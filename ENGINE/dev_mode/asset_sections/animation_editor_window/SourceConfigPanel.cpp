#include "SourceConfigPanel.hpp"

#include "AnimationDocument.hpp"
#include "AsyncTaskQueue.hpp"
#include "CroppingService.hpp"

namespace animation_editor {

SourceConfigPanel::SourceConfigPanel() = default;

void SourceConfigPanel::set_document(std::shared_ptr<AnimationDocument> document) {
    document_ = std::move(document);
    // TODO: Refresh local state based on document payload.
}

void SourceConfigPanel::set_animation_id(const std::string& animation_id) {
    animation_id_ = animation_id;
    // TODO: Reload source configuration fields from animation payload.
}

void SourceConfigPanel::set_bounds(const SDL_Rect& bounds) {
    bounds_ = bounds;
    // TODO: Layout import buttons and selectors.
}

void SourceConfigPanel::set_services(std::shared_ptr<CroppingService> cropping, std::shared_ptr<AsyncTaskQueue> tasks) {
    cropping_service_ = std::move(cropping);
    task_queue_ = std::move(tasks);
    // TODO: Wire up asynchronous import helpers with provided services.
}

void SourceConfigPanel::update() {
    // TODO: Update progress indicators for background imports.
}

void SourceConfigPanel::render(SDL_Renderer* renderer) const {
    (void)renderer;
    // TODO: Draw source selection controls and status messages.
}

bool SourceConfigPanel::handle_event(const SDL_Event& e) {
    (void)e;
    // TODO: Handle file picker launches and toggle switches.
    return false;
}

void SourceConfigPanel::import_from_folder() {
    // TODO: Trigger folder import workflow and update document payload.
}

void SourceConfigPanel::import_from_animation() {
    // TODO: Allow aliasing another animation's frames.
}

void SourceConfigPanel::import_from_gif() {
    // TODO: Convert GIF frames into numbered PNGs and update animation payload.
}

void SourceConfigPanel::import_from_png_sequence() {
    // TODO: Load a user-selected PNG sequence into the animation.
}

}  // namespace animation_editor

