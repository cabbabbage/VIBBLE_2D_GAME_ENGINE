#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "core/manifest/manifest_loader.hpp"

#include <filesystem>
#include <system_error>

namespace {

class ManifestFileGuard {
public:
    explicit ManifestFileGuard(const std::filesystem::path& original)
        : original_(original),
          backup_(original.parent_path() /
                   (original.filename().string() + ".missing_manifest_test")) {
        std::error_code cleanup_error;
        std::filesystem::remove(backup_, cleanup_error);

        std::error_code rename_error;
        std::filesystem::rename(original_, backup_, rename_error);
        renamed_ = !rename_error;
    }

    ~ManifestFileGuard() {
        if (!renamed_) {
            return;
        }

        std::error_code restore_error;
        std::filesystem::rename(backup_, original_, restore_error);
        if (!restore_error) {
            return;
        }

        restore_error.clear();
        std::filesystem::copy_file(backup_, original_, std::filesystem::copy_options::overwrite_existing, restore_error);
        if (!restore_error) {
            std::error_code remove_error;
            std::filesystem::remove(backup_, remove_error);
        }
    }

    [[nodiscard]] bool renamed() const { return renamed_; }

private:
    std::filesystem::path original_;
    std::filesystem::path backup_;
    bool                  renamed_{false};
};

} // namespace

TEST_CASE("load_manifest returns manifest data with required sections") {
    auto manifest = manifest::load_manifest();

    CHECK(manifest.assets.is_object());
    CHECK(manifest.maps.is_object());

    for (auto it = manifest.maps.begin(); it != manifest.maps.end(); ++it) {
        const auto& map_entry = it.value();
        if (!map_entry.is_object()) {
            continue;
        }
        CHECK(map_entry.contains("rooms_data"));
        CHECK(map_entry.at("rooms_data").is_object());
        CHECK(map_entry.contains("trails_data"));
        CHECK(map_entry.at("trails_data").is_object());
    }
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

TEST_CASE("load_manifest returns empty data when manifest file is missing") {
    const std::filesystem::path path = manifest::manifest_path();

    if (!std::filesystem::exists(path)) {
        auto manifest = manifest::load_manifest();
        CHECK(manifest.raw.is_object());
        CHECK(manifest.raw.contains("assets"));
        CHECK(manifest.raw.contains("maps"));
        CHECK(manifest.assets.is_object());
        CHECK(manifest.assets.empty());
        CHECK(manifest.maps.is_object());
        CHECK(manifest.maps.empty());
        return;
    }

    ManifestFileGuard guard(path);
    REQUIRE(guard.renamed());

    auto manifest = manifest::load_manifest();
    CHECK(manifest.raw.is_object());
    CHECK(manifest.raw.contains("assets"));
    CHECK(manifest.raw.contains("maps"));
    CHECK(manifest.assets.is_object());
    CHECK(manifest.assets.empty());
    CHECK(manifest.maps.is_object());
    CHECK(manifest.maps.empty());
}
