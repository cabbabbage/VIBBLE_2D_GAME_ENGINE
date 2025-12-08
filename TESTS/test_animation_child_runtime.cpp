/*
#include "doctest/doctest.h"

#include <vector>

#include "animation_update/child_attachment_controller.hpp"
#include "asset/animation.hpp"

TEST_CASE("Hidden child attachments restart from frame zero when revealed") {
    Animation child_anim;
    child_anim.loop = false;
    auto& child_path = child_anim.movement_path(0);
    child_path.resize(2);
    child_path[0].frame_index = 0;
    child_path[0].is_first = true;
    child_path[0].next = &child_path[1];
    child_path[1].frame_index = 1;
    child_path[1].is_last = true;
    child_path[1].prev = &child_path[0];
    child_path[1].next = nullptr;

    Asset::AnimationChildAttachment slot;
    slot.child_index = 0;
    slot.animation = &child_anim;
    slot.current_frame = child_anim.get_first_frame();

    std::vector<Asset::AnimationChildAttachment> slots;
    slots.push_back(slot);

    animation_update::child_attachments::ParentState parent_state;
    parent_state.position = SDL_Point{10, 20};
    parent_state.base_position = SDL_Point{10, 20}; // For tests, assume base is same as position
    parent_state.flipped = false;
    parent_state.animation_id = "custom_anim";

    AnimationChildFrameData visible{};
    visible.child_index = 0;
    visible.dx = 3;
    visible.dy = -2;
    visible.degree = 15.0f;
    visible.render_in_front = true;
    visible.visible = true;

    AnimationChildFrameData hidden = visible;
    hidden.visible = false;

    AnimationFrame frame_visible;
    frame_visible.frame_index = 0;
    frame_visible.children.push_back(visible);

    AnimationFrame frame_hidden;
    frame_hidden.frame_index = 1;
    frame_hidden.children.push_back(hidden);

    AnimationFrame frame_visible_again;
    frame_visible_again.frame_index = 2;
    frame_visible_again.children.push_back(visible);

    const float frame_dt = 1.0f / static_cast<float>(kBaseAnimationFps);

    auto step = [&](AnimationFrame* frame) {
        animation_update::child_attachments::advance_frames(slots, parent_state, frame_dt);
        animation_update::child_attachments::apply_frame_data(slots, parent_state, frame);
    };

    auto& slot_ref = slots.front();

    step(&frame_visible);
    REQUIRE(slot_ref.current_frame != nullptr);
    CHECK(slot_ref.current_frame->frame_index == 0);

    // Hide for multiple frames to let the attachment advance.
    for (int i = 0; i < 3; ++i) {
        step(&frame_hidden);
    }
    REQUIRE(slot_ref.current_frame != nullptr);
    CHECK(slot_ref.current_frame->frame_index == 1);

    step(&frame_visible_again);
    REQUIRE(slot_ref.current_frame != nullptr);
    CHECK(slot_ref.current_frame->frame_index == 0);
    CHECK(slot_ref.frame_progress == doctest::Approx(0.0f));
} */
