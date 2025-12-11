#include "doctest/doctest.h"

#include <nlohmann/json.hpp>

#include "asset/animation.hpp"
#include "asset/animation_loader.hpp"

namespace {
AnimationChildFrameData make_sample(int child_index, bool visible = false) {
    AnimationChildFrameData sample{};
    sample.child_index = child_index;
    sample.visible = visible;
    sample.render_in_front = true;
    sample.dx = 0;
    sample.dy = 0;
    sample.degree = 0.0f;
    return sample;
}

void seed_parent_frames(Animation& animation, std::size_t count) {
    auto& path = animation.movement_path(0);
    path.clear();
    path.resize(count);
    animation.frames.clear();
    for (std::size_t i = 0; i < count; ++i) {
        AnimationFrame& frame = path[i];
        frame.frame_index = static_cast<int>(i);
        frame.children.clear();
        frame.next = (i + 1 < count) ? &path[i + 1] : nullptr;
        frame.prev = (i > 0) ? &path[i - 1] : nullptr;
        frame.is_first = (i == 0);
        frame.is_last = (i + 1 == count);
        animation.frames.push_back(&frame);
    }
}

} // namespace

TEST_CASE("rebuild preserves async child timelines") {
    Animation animation;
    animation.child_assets() = {"childA"};
    seed_parent_frames(animation, 3);

    AnimationChildData async_descriptor;
    async_descriptor.asset_name = "childA";
    async_descriptor.mode = AnimationChildMode::Async;
    async_descriptor.frames = {make_sample(0), make_sample(0), make_sample(0)};
    animation.child_timelines().push_back(async_descriptor);

    animation.rebuild_child_timelines_from_frames();

    REQUIRE(animation.child_timelines().size() == 1);
    const auto& descriptor = animation.child_timelines().front();
    CHECK(descriptor.mode == AnimationChildMode::Async);
    CHECK(descriptor.frames.size() == 3);
}

TEST_CASE("rebuild sizes static timelines to match parent frames") {
    Animation animation;
    animation.child_assets() = {"childA"};
    seed_parent_frames(animation, 4);
    animation.frames[1]->children.push_back(make_sample(0, true));

    AnimationChildData static_descriptor;
    static_descriptor.asset_name = "childA";
    static_descriptor.mode = AnimationChildMode::Static;
    static_descriptor.frames = {make_sample(0)};
    animation.child_timelines().push_back(static_descriptor);

    animation.rebuild_child_timelines_from_frames();

    REQUIRE(animation.child_timelines().size() == 1);
    const auto& descriptor = animation.child_timelines().front();
    CHECK(descriptor.mode == AnimationChildMode::Static);
    CHECK(descriptor.frames.size() == 4);
    CHECK(descriptor.frames[1].visible);
}

TEST_CASE("loader rejects child timelines without explicit mode") {
    Animation animation;
    animation.child_assets() = {"childA"};
    seed_parent_frames(animation, 2);

    AnimationChildData previous;
    previous.asset_name = "childA";
    previous.mode = AnimationChildMode::Async;
    previous.frames = {make_sample(0)};
    animation.child_timelines().push_back(previous);

    nlohmann::json payload = nlohmann::json::object({
        {"child_timelines", nlohmann::json::array({
             nlohmann::json::object({{"child", 0}})
        })}
    });

    const bool loaded = AnimationLoader::load_child_timelines_from_json(payload, animation);
    CHECK_FALSE(loaded);
    REQUIRE(animation.child_timelines().size() == 1);
    CHECK(animation.child_timelines().front().mode == AnimationChildMode::Async);
}

TEST_CASE("loader sizes static timelines using parent frame count") {
    Animation animation;
    animation.child_assets() = {"childA"};
    seed_parent_frames(animation, 3);

    nlohmann::json payload = nlohmann::json::object({
        {"child_timelines", nlohmann::json::array({
             nlohmann::json::object({
                 {"child", 0},
                 {"mode", "static"},
                 {"frames", nlohmann::json::array({
                     nlohmann::json::object({{"dx", 1}, {"visible", true}})
                 })}
             })
        })}
    });

    REQUIRE(AnimationLoader::load_child_timelines_from_json(payload, animation));
    REQUIRE(animation.child_timelines().size() == 1);
    const auto& descriptor = animation.child_timelines().front();
    CHECK(descriptor.mode == AnimationChildMode::Static);
    CHECK(descriptor.frames.size() == 3);
    CHECK(descriptor.frames[0].visible);
}

TEST_CASE("loader inherits async frame count when frames omitted") {
    Animation animation;
    animation.child_assets() = {"childA"};
    seed_parent_frames(animation, 2);

    AnimationChildData previous;
    previous.asset_name = "childA";
    previous.mode = AnimationChildMode::Async;
    previous.frames = {make_sample(0), make_sample(0), make_sample(0), make_sample(0)};
    animation.child_timelines().push_back(previous);

    nlohmann::json payload = nlohmann::json::object({
        {"child_timelines", nlohmann::json::array({
             nlohmann::json::object({
                 {"child", 0},
                 {"mode", "async"}
             })
        })}
    });

    REQUIRE(AnimationLoader::load_child_timelines_from_json(payload, animation));
    REQUIRE(animation.child_timelines().size() == 1);
    const auto& descriptor = animation.child_timelines().front();
    CHECK(descriptor.mode == AnimationChildMode::Async);
    CHECK(descriptor.frames.size() == 4);
}
