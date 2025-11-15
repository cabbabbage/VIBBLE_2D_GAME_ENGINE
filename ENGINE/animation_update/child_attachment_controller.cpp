#include "animation_update/child_attachment_controller.hpp"

#include "animation_update/child_attachment_math.hpp"

namespace animation_update::child_attachments {

void update_dimensions(Asset::AnimationChildAttachment& slot) {
    slot.cached_w = 0;
    slot.cached_h = 0;
    if (!slot.animation || !slot.current_frame) {
        return;
    }
    SDL_Texture* texture = slot.animation->get_frame(slot.current_frame);
    if (!texture) {
        return;
    }
    int width = 0;
    int height = 0;
    if (SDL_QueryTexture(texture, nullptr, nullptr, &width, &height) == 0) {
        slot.cached_w = width;
        slot.cached_h = height;
    }
}

void restart(Asset::AnimationChildAttachment& slot) {
    slot.frame_progress = 0.0f;
    slot.cached_w = 0;
    slot.cached_h = 0;
    if (!slot.animation) {
        slot.current_frame = nullptr;
        return;
    }
    slot.current_frame = slot.animation->get_first_frame();
    update_dimensions(slot);
}

void advance_frames(std::vector<Asset::AnimationChildAttachment>& slots,
                    const ParentState& parent_state,
                    float dt) {
    if (slots.empty()) {
        return;
    }
    if (!(dt > 0.0f)) {
        dt = 1.0f / 60.0f;
    }
    for (auto& slot : slots) {
        if (!slot.animation || !slot.current_frame) {
            continue;
        }
        const AnimationFrame* previous_frame = slot.current_frame;
        int fps = slot.animation->playback_fps;
        if (fps <= 0) {
            fps = 24;
        }
        const float interval = 1.0f / static_cast<float>(fps);
        slot.frame_progress += dt;
        while (slot.frame_progress >= interval) {
            slot.frame_progress -= interval;
            if (slot.current_frame->next) {
                slot.current_frame = slot.current_frame->next;
            } else if (slot.animation->loop ||
                       parent_state.animation_id == animation_update::detail::kDefaultAnimation) {
                slot.current_frame = slot.animation->get_first_frame();
            } else {
                break;
            }
        }
        if (slot.current_frame != previous_frame) {
            update_dimensions(slot);
        }
    }
}

void apply_frame_data(std::vector<Asset::AnimationChildAttachment>& slots,
                      const ParentState& parent_state,
                      const AnimationFrame* frame) {
    if (slots.empty()) {
        return;
    }
    const int parent_frame_index = frame ? frame->frame_index : -1;
    for (auto& slot : slots) {
        const bool parent_looped = parent_frame_index != -1 &&
                                   slot.last_parent_frame_index != -1 &&
                                   parent_frame_index < slot.last_parent_frame_index;
        if (parent_looped) {
            restart(slot);
        }
        slot.last_parent_frame_index = parent_frame_index;
        slot.visible = false;
        slot.rotation_degrees = 0.0f;
        slot.world_pos = parent_state.position;
        slot.render_in_front = true;
    }
    if (!frame) {
        for (auto& slot : slots) {
            slot.was_visible = slot.visible;
        }
        return;
    }
    for (const auto& child_data : frame->children) {
        if (child_data.child_index < 0 ||
            child_data.child_index >= static_cast<int>(slots.size())) {
            continue;
        }
        auto& slot = slots[child_data.child_index];
        if (!slot.animation) {
            continue;
        }
        const bool became_visible = child_data.visible && !slot.was_visible;
        if (became_visible) {
            restart(slot);
        }
        slot.visible = child_data.visible;
        const int dx = parent_state.flipped ? -child_data.dx : child_data.dx;
        slot.world_pos.x = parent_state.position.x + dx;
        slot.world_pos.y = parent_state.position.y + child_data.dy;
        slot.rotation_degrees = mirrored_child_rotation(parent_state.flipped, child_data.degree);
        slot.render_in_front = child_data.render_in_front;
    }
    for (auto& slot : slots) {
        slot.was_visible = slot.visible;
    }
}

} // namespace animation_update::child_attachments
