#include "doctest/doctest.h"

#include <memory>
#include <string>
#include <vector>

#include "asset/asset_info.hpp"
#include "asset/animation.hpp"
#include "asset/animation_frame.hpp"

TEST_CASE("set_animation_children syncs animations and timelines") {
    auto info = std::make_shared<AssetInfo>();

    Animation anim;
    AnimationFrame f0;
    AnimationFrame f1;
    f0.frame_index = 0;
    f1.frame_index = 1;
    anim.frames = { &f0, &f1 };

    info->animations["default"] = anim;

    std::vector<std::string> children = { "child_a", "child_b" };
    info->set_animation_children(children);

    auto it = info->animations.find("default");
    REQUIRE(it != info->animations.end());
    const Animation& updated = it->second;

    REQUIRE(updated.child_assets().size() == 2);
    CHECK(updated.child_assets()[0] == "child_a");
    CHECK(updated.child_assets()[1] == "child_b");

    REQUIRE(updated.child_timelines().size() == 2);
    CHECK(updated.child_timelines()[0].asset_name == "child_a");
    CHECK(updated.child_timelines()[1].asset_name == "child_b");
    REQUIRE(updated.child_timelines()[0].frames.size() == updated.frames.size());
    REQUIRE(updated.child_timelines()[1].frames.size() == updated.frames.size());
    CHECK(updated.child_timelines()[0].frames.front().child_index == 0);
    CHECK(updated.child_timelines()[1].frames.front().child_index == 1);
}
