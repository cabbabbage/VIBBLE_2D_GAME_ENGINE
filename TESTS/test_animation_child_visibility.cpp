/*
#include "doctest/doctest.h"

#include <nlohmann/json.hpp>

#include "dev_mode/frame_editor_session.hpp"

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

TEST_CASE("Frame editor hydrates frames from child timelines") {
    FrameEditorSession session;
    session.child_assets_ = {"childA"};
    session.frames_.resize(1);
    session.sync_child_frames();

    nlohmann::json payload = nlohmann::json::object({
        {"child_timelines", nlohmann::json::array({
            nlohmann::json::object({
                {"child", 0},
                {"asset", "childA"},
                {"mode", "static"},
                {"frames", nlohmann::json::array({
                    nlohmann::json::object({
                        {"dx", 7},
                        {"dy", -2},
                        {"degree", 45.0},
                        {"visible", true},
                        {"render_in_front", false}
                    })
                })}
            })
        })}
    });

    session.apply_child_timelines_from_payload(payload);
    REQUIRE_FALSE(session.frames_.empty());
    REQUIRE(session.frames_.front().children.size() == 1);
    const auto& child = session.frames_.front().children.front();
    CHECK(child.has_data);
    CHECK(child.dx == doctest::Approx(7.0f));
    CHECK(child.dy == doctest::Approx(-2.0f));
    CHECK(child.degree == doctest::Approx(45.0f));
    CHECK(child.visible);
    CHECK_FALSE(child.render_in_front);
}

TEST_CASE("Frame editor serializes child timelines from frames") {
    FrameEditorSession session;
    session.child_assets_ = {"childA"};
    FrameEditorSession::MovementFrame frame;
    FrameEditorSession::ChildFrame child;
    child.child_index = 0;
    child.dx = 5.0f;
    child.dy = -3.0f;
    child.degree = 10.0f;
    child.visible = true;
    child.render_in_front = false;
    child.has_data = true;
    frame.children.push_back(child);
    session.frames_.push_back(frame);

    const nlohmann::json payload = nlohmann::json::object();
    const nlohmann::json timelines = session.build_child_timelines_payload(payload);
    REQUIRE(timelines.is_array());
    REQUIRE(timelines.size() == 1);
    const auto& entry = timelines.front();
    CHECK(entry.value("mode", "") == "static");
    REQUIRE(entry.contains("frames"));
    REQUIRE(entry["frames"].is_array());
    REQUIRE(entry["frames"].size() == 1);
    const auto& sample = entry["frames"][0];
    CHECK(sample.value("dx", 0) == 5);
    CHECK(sample.value("dy", 0) == -3);
    CHECK(sample.value("visible", false));
    CHECK_FALSE(sample.value("render_in_front", true));
    CHECK(sample.value("degree", 0.0) == doctest::Approx(10.0));
}

TEST_CASE("Child rotation mirrors when parent flips horizontally") {
    const float original = 20.0f;
    CHECK(mirrored_child_rotation(false, original) == doctest::Approx(original));
    CHECK(mirrored_child_rotation(true, original) == doctest::Approx(-original));
} */
