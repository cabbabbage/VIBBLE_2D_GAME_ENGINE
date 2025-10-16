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
