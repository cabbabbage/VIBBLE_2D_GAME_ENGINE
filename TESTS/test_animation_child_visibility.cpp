/*
#include "doctest/doctest.h"

#include <nlohmann/json.hpp>

#define private public
#include "dev_mode/frame_editor_session.hpp"
#undef private

#include "asset/animation_frame.hpp"
#include "animation_update/child_attachment_math.hpp"

TEST_CASE("AnimationChildFrameData defaults to visible when flag is omitted") {
    AnimationChildFrameData child;
    CHECK(child.visible);
    CHECK(child.render_in_front);
}

TEST_CASE("Frame editor keeps children visible when payload omits boolean") {
    nlohmann::json payload = nlohmann::json::object();
    payload["movement"] = nlohmann::json::array();
    payload["movement"].push_back(nlohmann::json::array({
        0,                                      // dx
        0,                                      // dy
        false,                                  // z resort
        nlohmann::json::array({255, 255, 255}), // rgb
        nlohmann::json::array({
            nlohmann::json::array({
                0,      // child index
                12,     // dx
                -3,     // dy
                15.0    // degree
                // visible flag intentionally omitted to exercise default
            })
        })
    }));

    const auto frames = FrameEditorSession::parse_movement_frames_json(payload.dump());
    REQUIRE(frames.size() == 1);
    REQUIRE(frames.front().children.size() == 1);

    const auto& parsed_child = frames.front().children.front();
    CHECK(parsed_child.visible);
    CHECK(parsed_child.render_in_front);
}

TEST_CASE("Child rotation mirrors when parent flips horizontally") {
    const float original = 20.0f;
    CHECK(mirrored_child_rotation(false, original) == doctest::Approx(original));
    CHECK(mirrored_child_rotation(true, original) == doctest::Approx(-original));
} */
