#include "AnimationDocument.hpp"

namespace animation_editor {

AnimationDocument::AnimationDocument() = default;

void AnimationDocument::load_from_file(const std::filesystem::path& info_path) {
    (void)info_path;
    // TODO: Parse info.json, normalize payloads, and populate animation cache.
}

void AnimationDocument::save_to_file() const {
    // TODO: Serialize current animation definitions back to info.json with proper formatting.
}

void AnimationDocument::create_animation(const std::string& animation_id) {
    (void)animation_id;
    // TODO: Insert a new animation definition with default payload into the document.
}

void AnimationDocument::delete_animation(const std::string& animation_id) {
    (void)animation_id;
    // TODO: Remove the specified animation and handle start animation fallback if needed.
}

std::vector<std::string> AnimationDocument::animation_ids() const {
    // TODO: Return animation ids in stable order for UI presentation.
    return {};
}

std::optional<std::string> AnimationDocument::start_animation() const {
    // TODO: Provide the currently selected start animation id.
    return std::nullopt;
}

void AnimationDocument::set_start_animation(const std::string& animation_id) {
    (void)animation_id;
    // TODO: Update the start animation pointer and mark the document dirty for save prompts.
}

void AnimationDocument::rename_animation(const std::string& old_id, const std::string& new_id) {
    (void)old_id;
    (void)new_id;
    // TODO: Rename an animation while keeping payload and references intact.
}

void AnimationDocument::replace_animation_payload(const std::string& animation_id, const std::string& payload_json) {
    (void)animation_id;
    (void)payload_json;
    // TODO: Replace the stored payload for the given animation id.
}

void AnimationDocument::ensure_document_initialized() {
    // TODO: Guarantee the JSON structure exists, mirroring Python _coerce_payload logic.
}

void AnimationDocument::rebuild_animation_cache() {
    // TODO: Recompute the in-memory animation map after edits or loads.
}

}  // namespace animation_editor

