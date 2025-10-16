#include "doctest/doctest.h"

#include <filesystem>
#include <nlohmann/json.hpp>

#include "core/manifest/manifest_loader.hpp"
#include "dev_mode/core/manifest_store.hpp"
#include "dev_mode/room_editor_map_info.hpp"
#include "map_generation/map_layers_geometry.hpp"

namespace {

devmode::core::ManifestStore make_store(const std::filesystem::path& manifest_path,
                                        nlohmann::json manifest,
                                        nlohmann::json& captured_submission) {
    manifest::ManifestData data;
    data.raw = manifest;
    data.assets = manifest.value("assets", nlohmann::json::object());
    data.maps = manifest.value("maps", nlohmann::json::object());
    data.rooms = manifest.value("rooms", nlohmann::json::array());

    return devmode::core::ManifestStore(
        manifest_path,
        [data]() mutable {
            return data;
        },
        [&captured_submission](const std::filesystem::path&, const nlohmann::json& payload, int) {
            captured_submission = payload;
        },
        []() {},
        2);
}

}  // namespace

TEST_CASE("RoomEditor resolves map info from manifest when no file is present") {
    namespace fs = std::filesystem;
    const fs::path manifest_path = fs::temp_directory_path() / "room_editor_manifest_only.json";

    nlohmann::json map_entry = {
        {"map_layers", {{{"rooms", {{{"name", "CenterRoom"}}}}}}},
        {"rooms_data", {
            {"CenterRoom",
             {{"geometry", "Square"}, {"max_width", 200}, {"max_height", 100}}}
        }}
    };

    nlohmann::json manifest = {
        {"assets", nlohmann::json::object()},
        {"maps", {{"ROOM_MAP", map_entry}}},
        {"rooms", nlohmann::json::array()},
        {"version", 1}
    };

    nlohmann::json submitted;
    auto store = make_store(manifest_path, manifest, submitted);

    const nlohmann::json resolved = devmode::room_editor_detail::resolve_map_info_blob(
        nullptr,
        &store,
        "ROOM_MAP");

    REQUIRE(resolved.is_object());
    CHECK(resolved == map_entry);

    const double map_radius = map_layers::map_radius_from_map_info(resolved);
    CHECK(map_radius > map_layers::kMapRadiusOuterPadding);
}

