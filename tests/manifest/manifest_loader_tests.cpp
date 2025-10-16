#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "core/manifest/manifest_loader.hpp"

TEST_CASE("load_manifest returns manifest data with required sections") {
    auto manifest = manifest::load_manifest();

    CHECK(manifest.assets.is_object());
    CHECK(manifest.maps.is_object());
    CHECK(manifest.rooms.is_array());
}

TEST_CASE("manifest_path points to manifest json in project root") {
    const std::string path = manifest::manifest_path();

    CHECK(path.find("manifest.json") != std::string::npos);
}

TEST_CASE("manifest maps expose lighting and audio metadata") {
    auto manifest = manifest::load_manifest();

    REQUIRE(manifest.maps.is_object());
    auto map_it = manifest.maps.find("FORREST");
    REQUIRE(map_it != manifest.maps.end());
    const auto& forrest = map_it.value();
    REQUIRE(forrest.is_object());

    CHECK(forrest.contains("map_light_data"));
    CHECK(forrest.at("map_light_data").is_object());
    CHECK(forrest.contains("map_assets_data"));
    CHECK(forrest.at("map_assets_data").is_object());

    CHECK(forrest.contains("audio"));
    const auto& audio = forrest.at("audio");
    CHECK(audio.is_object());
    if (audio.contains("music")) {
        const auto& music = audio.at("music");
        CHECK(music.is_object());
        if (music.contains("tracks")) {
            CHECK(music.at("tracks").is_array());
        }
    }
}
