#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "asset/asset_info.hpp"
#include "asset/asset_types.hpp"

#include <filesystem>
#include <fstream>
#include <string>

#include "core/manifest/manifest_loader.hpp"
#include "dev_mode/core/manifest_store.hpp"
#include "dev_mode/core/dev_json_store.hpp"

namespace {
namespace fs = std::filesystem;

fs::path make_manifest_path(const std::string& test_name, const nlohmann::json& payload) {
    fs::path root = fs::temp_directory_path() / "vibble_asset_info_manifest_tests" / test_name;
    fs::create_directories(root);
    fs::path manifest = root / "manifest.json";
    std::ofstream out(manifest);
    out << payload.dump(2);
    return manifest;
}

nlohmann::json read_json(const fs::path& path) {
    std::ifstream in(path);
    nlohmann::json parsed = nlohmann::json::object();
    if (in.is_open()) {
        in >> parsed;
    }
    return parsed;
}

manifest::ManifestData load_manifest_from_path(const fs::path& path) {
    manifest::ManifestData data;
    data.raw = read_json(path);
    data.assets = data.raw.contains("assets") ? data.raw["assets"] : nlohmann::json::object();
    data.maps = data.raw.contains("maps") ? data.raw["maps"] : nlohmann::json::object();
    return data;
}

} // namespace

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

TEST_CASE("AssetInfo commit_manifest persists changes via ManifestStore") {
    nlohmann::json initial = {
        {"assets", {
            {"ManifestCommit", {
                {"asset_name", "ManifestCommit"},
                {"asset_type", "Object"},
                {"z_threshold", 5},
                {"tags", nlohmann::json::array({"passable"})},
                {"anti_tags", nlohmann::json::array()},
                {"neighbor_search_distance", 100}
            }}
        }},
        {"maps", nlohmann::json::object()}
    };

    const auto manifest_path = make_manifest_path("commit_manifest", initial);

    auto loader = [manifest_path]() {
        return load_manifest_from_path(manifest_path);
    };

    devmode::core::ManifestStore store(manifest_path, loader);
    AssetInfo::set_manifest_store_provider([&store]() -> devmode::core::ManifestStore* {
        return &store;
    });

    auto metadata = initial["assets"]["ManifestCommit"];
    auto info = AssetInfo::from_manifest_entry("ManifestCommit", metadata);

    REQUIRE(info);
    info->set_z_threshold(42);
    info->set_neighbor_search_radius(256);
    info->add_tag("fresh");
    info->remove_tag("passable");
    info->set_passable(false);

    AssetInfo::ChildInfo absolute_child{};
    absolute_child.area_name = "absolute";
    absolute_child.z_offset = 1;
    absolute_child.inline_assets = nlohmann::json::array();
    const auto asset_dir = fs::path(info->asset_dir_path());
    const auto absolute_child_path = fs::absolute(asset_dir / "children" / "abs.json");
    absolute_child.json_path = absolute_child_path.string();

    AssetInfo::ChildInfo relative_child{};
    relative_child.area_name = "relative";
    relative_child.z_offset = 2;
    relative_child.inline_assets = nlohmann::json::array();
    relative_child.json_path = (asset_dir / "children" / "rel.json").generic_string();

    info->set_children({absolute_child, relative_child});

    CHECK(info->commit_manifest());
    store.flush();

    auto persisted = read_json(manifest_path);
    auto& asset_json = persisted["assets"]["ManifestCommit"];
    CHECK(asset_json["z_threshold"].get<int>() == 42);
    CHECK(asset_json["neighbor_search_distance"].get<int>() == 256);
    REQUIRE(asset_json["tags"].is_array());
    CHECK(asset_json["tags"].size() == 1);
    CHECK(asset_json["tags"][0].get<std::string>() == "fresh");
    REQUIRE(asset_json["child_assets"].is_array());
    REQUIRE(asset_json["child_assets"].size() == 2);
    CHECK(asset_json["child_assets"][0]["json_path"].get<std::string>() == "children/abs.json");
    CHECK(asset_json["child_assets"][1]["json_path"].get<std::string>() == "children/rel.json");

    store.reload();
    auto view = store.get_asset("ManifestCommit");
    REQUIRE(view);
    auto rehydrated = AssetInfo::from_manifest_entry(view.name, *view.data);
    REQUIRE(rehydrated);
    CHECK(rehydrated->z_threshold == 42);
    CHECK(rehydrated->NeighborSearchRadius == 256);
    CHECK_FALSE(rehydrated->passable);
    CHECK_FALSE(rehydrated->has_tag("passable"));
    CHECK(rehydrated->has_tag("fresh"));
    REQUIRE(rehydrated->children.size() == 2);
    CHECK(fs::path(rehydrated->children[0].json_path) == fs::path(info->asset_dir_path()) / "children" / "abs.json");
    CHECK(fs::path(rehydrated->children[1].json_path) == fs::path(info->asset_dir_path()) / "children" / "rel.json");

    AssetInfo::set_manifest_store_provider({});
    devmode::core::DevJsonStore::instance().flush_all();
    fs::remove_all(manifest_path.parent_path());
}

TEST_CASE("AssetInfo reload_animations_from_disk pulls updates from manifest store") {
    nlohmann::json manifest = {
        {"assets", {
            {"ReloadManifest", {
                {"asset_name", "ReloadManifest"},
                {"asset_type", "Object"},
                {"start", "idle"},
                {"animations", {
                    {"idle", {
                        {"source", {
                            {"kind", "folder"},
                            {"path", "SRC/assets/ReloadManifest/idle"}
                        }},
                        {"locked", false},
                        {"speed_factor", 1.0},
                        {"number_of_frames", 1}
                    }}
                }}
            }}
        }},
        {"maps", nlohmann::json::object()}
    };

    const auto manifest_path = make_manifest_path("reload_animations_manifest", manifest);

    auto loader = [manifest_path]() {
        return load_manifest_from_path(manifest_path);
    };

    devmode::core::ManifestStore store(manifest_path, loader);
    AssetInfo::set_manifest_store_provider([&store]() -> devmode::core::ManifestStore* {
        return &store;
    });

    auto view = store.get_asset("ReloadManifest");
    REQUIRE(view);
    auto info = AssetInfo::from_manifest_entry(view.name, *view.data);
    REQUIRE(info);

    CHECK(info->info_json_path().empty());
    CHECK(info->start_animation == "idle");
    auto names = info->animation_names();
    REQUIRE(names.size() == 1);
    CHECK(names.front() == "idle");

    auto edit = store.begin_asset_edit("ReloadManifest");
    REQUIRE(edit);
    auto& draft = edit.data();
    draft["animations"] = nlohmann::json{
        {"run", {
            {"frames_path", "run"},
            {"lock_until_done", true},
            {"speed", 2.0}
        }}
    };
    draft["start"] = "run";
    REQUIRE(edit.commit());

    CHECK(info->reload_animations_from_disk());

    auto updated_names = info->animation_names();
    REQUIRE(updated_names.size() == 1);
    CHECK(updated_names.front() == "run");
    CHECK(info->start_animation == "run");

    auto run_payload = info->animation_payload("run");
    REQUIRE(run_payload.is_object());
    REQUIRE(run_payload["source"].is_object());
    CHECK(run_payload["source"].value("kind", "") == "folder");
    CHECK(run_payload["source"].value("path", "") == "run");
    CHECK(run_payload.value("locked", false));
    CHECK(run_payload.value("speed_factor", 1.0f) == doctest::Approx(2.0f));
    CHECK(info->animation_payload("idle").empty());

    AssetInfo::set_manifest_store_provider({});
    devmode::core::DevJsonStore::instance().flush_all();
    fs::remove_all(manifest_path.parent_path());
}

TEST_CASE("AssetInfo reload_animations_from_disk pulls updates from manifest store") {
    nlohmann::json manifest = {
        {"assets", {
            {"ReloadManifest", {
                {"asset_name", "ReloadManifest"},
                {"asset_type", "Object"},
                {"start", "idle"},
                {"animations", {
                    {"idle", {
                        {"source", {
                            {"kind", "folder"},
                            {"path", "SRC/assets/ReloadManifest/idle"}
                        }},
                        {"locked", false},
                        {"speed_factor", 1.0},
                        {"number_of_frames", 1}
                    }}
                }}
            }}
        }},
        {"maps", nlohmann::json::object()}
    };

    const auto manifest_path = make_manifest_path("reload_animations_manifest", manifest);

    auto loader = [manifest_path]() {
        return load_manifest_from_path(manifest_path);
    };

    devmode::core::ManifestStore store(manifest_path, loader);
    AssetInfo::set_manifest_store_provider([&store]() -> devmode::core::ManifestStore* {
        return &store;
    });

    auto view = store.get_asset("ReloadManifest");
    REQUIRE(view);
    auto info = AssetInfo::from_manifest_entry(view.name, *view.data);
    REQUIRE(info);

    CHECK(info->info_json_path().empty());
    CHECK(info->start_animation == "idle");
    auto names = info->animation_names();
    REQUIRE(names.size() == 1);
    CHECK(names.front() == "idle");

    auto edit = store.begin_asset_edit("ReloadManifest");
    REQUIRE(edit);
    auto& draft = edit.data();
    draft["animations"] = nlohmann::json{
        {"run", {
            {"frames_path", "run"},
            {"lock_until_done", true},
            {"speed", 2.0}
        }}
    };
    draft["start"] = "run";
    REQUIRE(edit.commit());

    CHECK(info->reload_animations_from_disk());

    auto updated_names = info->animation_names();
    REQUIRE(updated_names.size() == 1);
    CHECK(updated_names.front() == "run");
    CHECK(info->start_animation == "run");

    auto run_payload = info->animation_payload("run");
    REQUIRE(run_payload.is_object());
    REQUIRE(run_payload["source"].is_object());
    CHECK(run_payload["source"].value("kind", "") == "folder");
    CHECK(run_payload["source"].value("path", "") == "run");
    CHECK(run_payload.value("locked", false));
    CHECK(run_payload.value("speed_factor", 1.0f) == doctest::Approx(2.0f));
    CHECK(info->animation_payload("idle").empty());

    AssetInfo::set_manifest_store_provider({});
    devmode::core::DevJsonStore::instance().flush_all();
    fs::remove_all(manifest_path.parent_path());
}

TEST_CASE("AssetInfo reload_animations_from_disk pulls updates from manifest store") {
    nlohmann::json manifest = {
        {"assets", {
            {"ReloadManifest", {
                {"asset_name", "ReloadManifest"},
                {"asset_type", "Object"},
                {"start", "idle"},
                {"animations", {
                    {"idle", {
                        {"source", {
                            {"kind", "folder"},
                            {"path", "SRC/assets/ReloadManifest/idle"}
                        }},
                        {"locked", false},
                        {"speed_factor", 1.0},
                        {"number_of_frames", 1}
                    }}
                }}
            }}
        }},
        {"maps", nlohmann::json::object()}
    };

    const auto manifest_path = make_manifest_path("reload_animations_manifest", manifest);

    auto loader = [manifest_path]() {
        return load_manifest_from_path(manifest_path);
    };

    devmode::core::ManifestStore store(manifest_path, loader);
    AssetInfo::set_manifest_store_provider([&store]() -> devmode::core::ManifestStore* {
        return &store;
    });

    auto view = store.get_asset("ReloadManifest");
    REQUIRE(view);
    auto info = AssetInfo::from_manifest_entry(view.name, *view.data);
    REQUIRE(info);

    CHECK(info->info_json_path().empty());
    CHECK(info->start_animation == "idle");
    auto names = info->animation_names();
    REQUIRE(names.size() == 1);
    CHECK(names.front() == "idle");

    auto edit = store.begin_asset_edit("ReloadManifest");
    REQUIRE(edit);
    auto& draft = edit.data();
    draft["animations"] = nlohmann::json{
        {"run", {
            {"frames_path", "run"},
            {"lock_until_done", true},
            {"speed", 2.0}
        }}
    };
    draft["start"] = "run";
    REQUIRE(edit.commit());

    CHECK(info->reload_animations_from_disk());

    auto updated_names = info->animation_names();
    REQUIRE(updated_names.size() == 1);
    CHECK(updated_names.front() == "run");
    CHECK(info->start_animation == "run");

    auto run_payload = info->animation_payload("run");
    REQUIRE(run_payload.is_object());
    REQUIRE(run_payload["source"].is_object());
    CHECK(run_payload["source"].value("kind", "") == "folder");
    CHECK(run_payload["source"].value("path", "") == "run");
    CHECK(run_payload.value("locked", false));
    CHECK(run_payload.value("speed_factor", 1.0f) == doctest::Approx(2.0f));
    CHECK(info->animation_payload("idle").empty());

    AssetInfo::set_manifest_store_provider({});
    devmode::core::DevJsonStore::instance().flush_all();
    fs::remove_all(manifest_path.parent_path());
}
