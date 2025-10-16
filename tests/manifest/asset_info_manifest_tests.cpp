#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "asset/asset_info.hpp"
#include "asset/asset_types.hpp"

#include <filesystem>

TEST_CASE("AssetInfo manifest constructor populates metadata without disk access") {
    nlohmann::json metadata = {
        {"asset_name", "manifest_test"},
        {"asset_type", "Object"},
        {"start", "idle"},
        {"z_threshold", 7},
        {"min_same_type_distance", 4},
        {"min_distance_all", 2},
        {"neighbor_search_distance", 123},
        {"generate_rays", true},
        {"ray_strength", 54},
        {"tags", nlohmann::json::array({"passable", "pixel_art"})},
        {"anti_tags", nlohmann::json::array({"no_spawn"})},
        {"size_settings", {
            {"scale_percentage", 125.0},
            {"scale_filter", "nearest"}
        }},
        {"animations", {
            {"idle", {
                {"source", {
                    {"kind", "folder"},
                    {"path", "SRC/assets/manifest_test/idle"}
                }},
                {"locked", false},
                {"speed_factor", 1.0},
                {"number_of_frames", 1}
            }},
            {"walk", {
                {"frames_path", "walk"},
                {"lock_until_done", true},
                {"speed", 2.5}
            }}
        }},
        {"mappings", {
            {"looping", nlohmann::json::array({
                nlohmann::json{
                    {"condition", "true"},
                    {"map_to", {
                        {"options", nlohmann::json::array({
                            nlohmann::json{{"animation", "walk"}, {"percent", 100.0}}
                        })}
                    }}
                }
            })}
        }},
        {"child_assets", nlohmann::json::array({
            nlohmann::json{
                {"json_path", "children/child.json"},
                {"area_name", "core"},
                {"z_offset", 3},
                {"spawn_groups", nlohmann::json::array({
                    nlohmann::json{
                        {"display_name", "inline"},
                        {"candidates", nlohmann::json::array()},
                        {"max_number", 1},
                        {"min_number", 1},
                        {"position", "Exact"}
                    }
                })}
            }
        })},
        {"lighting_info", nlohmann::json::array({
            nlohmann::json{
                {"has_light_source", true},
                {"light_intensity", 200},
                {"radius", 400},
                {"offset_x", 5},
                {"offset_y", -10},
                {"factor", 80}
            }
        })},
        {"custom_controller_key", "test_controller"}
    };

    AssetInfo info("manifest_test", metadata);

    CHECK(info.info_json_path().empty());
    const std::filesystem::path expected_dir = std::filesystem::path("SRC") / "assets" / "manifest_test";
    CHECK(std::filesystem::path(info.asset_dir_path()) == expected_dir);
    CHECK(info.name == "manifest_test");

    CHECK(info.type == std::string(asset_types::object));
    CHECK(info.start_animation == "idle");
    CHECK(info.z_threshold == 7);
    CHECK(info.passable);
    CHECK(info.min_same_type_distance == 4);
    CHECK(info.min_distance_all == 2);
    CHECK(info.generate_rays);
    CHECK(info.ray_strength == 54);
    CHECK(info.NeighborSearchRadius == 123);

    CHECK(info.has_tag("passable"));
    CHECK_FALSE(info.has_tag("no_spawn"));
    CHECK(info.tag_lookup().count("pixel_art") == 1);
    CHECK(info.anti_tag_lookup().count("no_spawn") == 1);

    CHECK(doctest::Approx(info.scale_factor) == 1.25f);
    CHECK_FALSE(info.smooth_scaling);

    auto names = info.animation_names();
    CHECK(names.size() == 2);
    CHECK(names.front() == "idle");
    CHECK(names.back() == "walk");

    auto walk_payload = info.animation_payload("walk");
    REQUIRE(walk_payload.is_object());
    CHECK(walk_payload["source"].is_object());
    CHECK(walk_payload["source"].value("kind", "") == "folder");
    CHECK(walk_payload["source"].value("path", "") == "walk");
    CHECK(walk_payload.value("locked", false));
    CHECK(walk_payload.value("speed_factor", 1.0) == doctest::Approx(2.5));

    CHECK(info.pick_next_animation("looping") == "walk");

    REQUIRE(info.children.size() == 1);
    const std::filesystem::path expected_child = expected_dir / "children" / "child.json";
    CHECK(std::filesystem::path(info.children.front().json_path) == expected_child);
    CHECK(info.children.front().area_name == "core");
    CHECK(info.children.front().z_offset == 3);
    CHECK(info.children.front().inline_assets.is_array());

    CHECK(info.light_sources.size() == 1);
    CHECK(info.is_light_source);
    CHECK(info.custom_controller_key == "test_controller");
}
