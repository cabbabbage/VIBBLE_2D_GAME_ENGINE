#include "doctest/doctest.h"

#include "asset/animation.hpp"
#include "dev_mode/asset_sections/animation_editor_window/string_utils.hpp"

TEST_CASE("Reserved animation names recognized") {
    using animation_editor::strings::is_reserved_animation_name;
    CHECK(is_reserved_animation_name("kill"));
    CHECK(is_reserved_animation_name("Kill"));
    CHECK(is_reserved_animation_name("LOCK"));
    CHECK(is_reserved_animation_name("Reverse"));
    CHECK_FALSE(is_reserved_animation_name("default"));
    CHECK_FALSE(is_reserved_animation_name("walk"));
}

TEST_CASE("Animation on_end directive classification") {
    using Directive = Animation::OnEndDirective;
    CHECK(Animation::classify_on_end("default") == Directive::Default);
    CHECK(Animation::classify_on_end("") == Directive::Default);
    CHECK(Animation::classify_on_end("kill") == Directive::Kill);
    CHECK(Animation::classify_on_end("LOCK") == Directive::Lock);
    CHECK(Animation::classify_on_end("reverse") == Directive::Reverse);
    CHECK(Animation::classify_on_end("Idle") == Directive::Animation);
}
