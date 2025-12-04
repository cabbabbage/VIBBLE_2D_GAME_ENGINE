#include "doctest/doctest.h"

#include <vector>

#include "asset/animation_cloner.hpp"

namespace {
AnimationChildFrameData make_child(int idx, int dx, int dy) {
    AnimationChildFrameData child;
    child.child_index = idx;
    child.dx = dx;
    child.dy = dy;
    return child;
}
}

TEST_CASE("ApplyChildFrameFlip leaves offsets untouched when no flips requested") {
    std::vector<AnimationChildFrameData> children = {
        make_child(0, 12, -6),
        make_child(1, -4, 8)
    };
    AnimationCloner::Options opts;

    AnimationCloner::ApplyChildFrameFlip(children, opts);

    REQUIRE(children.size() == 2);
    CHECK(children[0].dx == 12);
    CHECK(children[0].dy == -6);
    CHECK(children[1].dx == -4);
    CHECK(children[1].dy == 8);
}

TEST_CASE("ApplyChildFrameFlip mirrors texture flips around bottom-center") {
    std::vector<AnimationChildFrameData> children = { make_child(0, 14, -10) };
    AnimationCloner::Options opts;
    opts.flip_horizontal = true;

    AnimationCloner::ApplyChildFrameFlip(children, opts);

    REQUIRE(children.size() == 1);
    CHECK(children[0].dx == -14);
    CHECK(children[0].dy == -10);
}

TEST_CASE("ApplyChildFrameFlip mirrors movement flips using the same pivot") {
    std::vector<AnimationChildFrameData> children = { make_child(0, -9, 7) };
    AnimationCloner::Options opts;
    opts.flip_movement_horizontal = true;
    opts.flip_movement_vertical = true;

    AnimationCloner::ApplyChildFrameFlip(children, opts);

    REQUIRE(children.size() == 1);
    CHECK(children[0].dx == 9);
    CHECK(children[0].dy == -7);
}

TEST_CASE("ApplyChildFrameFlip combines texture and movement flip requests once per axis") {
    std::vector<AnimationChildFrameData> children = { make_child(0, 5, -3) };
    AnimationCloner::Options opts;
    opts.flip_horizontal = true;
    opts.flip_movement_horizontal = true;
    opts.flip_vertical = true;

    AnimationCloner::ApplyChildFrameFlip(children, opts);

    REQUIRE(children.size() == 1);
    CHECK(children[0].dx == -5);
    CHECK(children[0].dy == 3);
}
