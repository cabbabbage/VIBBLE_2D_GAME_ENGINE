#include "animation_update/child_attachment_controller.hpp"

#include <mutex>
#include <random>
#include <iostream>
#include <cmath>

#include "animation_update/child_attachment_math.hpp"

namespace {
constexpr bool kChildAttachmentDebug = false;

std::mt19937& child_rng() {
    static std::mt19937 rng{ std::random_device{}() };
    return rng;
}

std::mutex& child_rng_mutex() {
    static std::mutex m;
    return m;
}

const AnimationFrame* pick_start_frame(const Animation& animation) {
    const AnimationFrame* start = animation.get_first_frame();
    if (!start) {
        return nullptr;
    }
    const bool should_randomize =
        (animation.randomize || animation.rnd_start) && animation.frames.size() > 1;
    if (!should_randomize) {
        return start;
    }
    std::uniform_int_distribution<int> dist(0, static_cast<int>(animation.frames.size()) - 1);
    int idx = 0;
    {
        std::lock_guard<std::mutex> lock(child_rng_mutex());
        idx = dist(child_rng());
    }
    const AnimationFrame* frame = start;
    while (idx-- > 0 && frame && frame->next) {
        frame = frame->next;
    }
    return frame ? frame : start;
}
}

namespace animation_update::child_attachments {

void update_dimensions(Asset::AnimationChildAttachment& slot) {
    slot.cached_w = 0;
    slot.cached_h = 0;
    if (!slot.animation || !slot.current_frame || slot.current_frame->variants.empty()) {
        return;
    }
    SDL_Texture* texture = slot.current_frame->variants[0].base_texture;
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
    slot.current_frame = pick_start_frame(*slot.animation);
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
        if (!slot.animation || !slot.current_frame || !slot.visible || slot.child_index < 0) {
            continue;
        }
        const AnimationFrame* previous_frame = slot.current_frame;
        const float interval = 1.0f / static_cast<float>(kBaseAnimationFps);
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
            if constexpr (kChildAttachmentDebug) {
                std::cout << "[ChildAttachments] Slot " << slot.child_index
                          << " advanced to frame "
                          << (slot.current_frame ? slot.current_frame->frame_index : -1) << " (asset='" << slot.asset_name << "')\n";
            }
        }
    }
}

void apply_frame_data(std::vector<Asset::AnimationChildAttachment>& slots,
                      const ParentState& parent_state,
                      const AnimationFrame* frame,
                      const std::vector<AnimationChildFrameData>* override_children) {
    if (slots.empty()) {
        return;
    }
    const float parent_scale = std::isfinite(parent_state.scale) && parent_state.scale > 0.0f ? parent_state.scale : 1.0f;
    const int parent_frame_index = frame ? frame->frame_index : -1;
    if constexpr (kChildAttachmentDebug) {
        std::cout << "[ChildAttachments] Applying frame data (parent_frame_index=" << parent_frame_index << ")\n";
    }
    for (auto& slot : slots) {
        const bool inactive = slot.child_index < 0;
        const bool parent_looped = parent_frame_index != -1 &&
                                   slot.last_parent_frame_index != -1 &&
                                   parent_frame_index < slot.last_parent_frame_index;
        if (parent_looped && !inactive) {
            restart(slot);
        }
        slot.last_parent_frame_index = parent_frame_index;
        slot.visible = false;
        slot.rotation_degrees = 0.0f;
        slot.render_in_front = true;
        if (inactive) {
            continue;
        }
    }
    const std::vector<AnimationChildFrameData>* child_entries = override_children;
    if (!child_entries && frame) {
        child_entries = &frame->children;
    }
    if (!child_entries) {
        for (auto& slot : slots) {
            slot.was_visible = slot.visible;
        }
        return;
    }
    for (const auto& child_data : *child_entries) {
        if (child_data.child_index < 0 ||
            child_data.child_index >= static_cast<int>(slots.size())) {
            if constexpr (kChildAttachmentDebug) {
                std::cout << "[ChildAttachments] Skipping child_data with out-of-range index "
                          << child_data.child_index << "\n";
            }
            continue;
        }
        auto& slot = slots[child_data.child_index];
        if (!slot.animation) {
            if constexpr (kChildAttachmentDebug) {
                std::cout << "[ChildAttachments] Slot " << child_data.child_index
                          << " has no bound animation (asset='" << slot.asset_name << "')\n";
            }
            continue;
        }
        if (!child_data.visible) {
            slot.visible = false;
            slot.render_in_front = child_data.render_in_front;
            if constexpr (kChildAttachmentDebug) {
                std::cout << "[ChildAttachments] Setting slot " << slot.child_index << " ('" << slot.asset_name
                          << "') visible=false\n";
            }
            continue;
        }
        const bool became_visible = !slot.was_visible;
        if (became_visible) {
            restart(slot);
        }
        slot.visible = true;
        if constexpr (kChildAttachmentDebug) {
            std::cout << "[ChildAttachments] Setting slot " << slot.child_index << " ('" << slot.asset_name
                      << "') visible=true dx=" << child_data.dx << " dy=" << child_data.dy
                      << " deg=" << child_data.degree << "\n";
        }
        const float scaled_dx = static_cast<float>(child_data.dx) * parent_scale;
        const float scaled_dy = static_cast<float>(child_data.dy) * parent_scale;

        const int dx = parent_state.flipped
                           ? -static_cast<int>(std::lround(scaled_dx)) : static_cast<int>(std::lround(scaled_dx));
        const int dy = static_cast<int>(std::lround(scaled_dy));
        slot.world_pos.x = parent_state.base_position.x + dx;
        slot.world_pos.y = parent_state.base_position.y + dy;
        slot.rotation_degrees = mirrored_child_rotation(parent_state.flipped, child_data.degree);
        slot.render_in_front = child_data.render_in_front;
    }
    for (auto& slot : slots) {
        slot.was_visible = slot.visible;
    }
}

}
