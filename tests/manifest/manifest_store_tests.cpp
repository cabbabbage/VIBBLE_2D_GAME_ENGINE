#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include "dev_mode/core/manifest_store.hpp"
#include "dev_mode/core/dev_json_store.hpp"
#include "dev_mode/manifest_spawn_group_utils.hpp"

namespace {
namespace fs = std::filesystem;

fs::path make_manifest_path(const std::string& test_name, const nlohmann::json& payload) {
    fs::path root = fs::temp_directory_path() / "vibble_manifest_store_tests" / test_name;
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
    data.rooms = data.raw.contains("rooms") ? data.raw["rooms"] : nlohmann::json::array();
    return data;
}

} // namespace

TEST_CASE("ManifestStore defers writes until flush") {
    nlohmann::json initial = {
        {"assets", {
            {"TestAsset", {
                {"value", 1}
            }}
        }},
        {"maps", nlohmann::json::object()},
        {"rooms", nlohmann::json::array()}
    };

    const auto manifest_path = make_manifest_path("defer_writes", initial);

    auto loader = [manifest_path]() {
        return load_manifest_from_path(manifest_path);
    };

    devmode::core::ManifestStore store(manifest_path, loader);

    auto session = store.begin_asset_edit("TestAsset");
    REQUIRE(session);
    session.data()["value"] = 42;
    CHECK(session.commit());

    auto on_disk = read_json(manifest_path);
    CHECK(on_disk["assets"]["TestAsset"]["value"].get<int>() == 1);

    store.flush();

    auto updated = read_json(manifest_path);
    CHECK(updated["assets"]["TestAsset"]["value"].get<int>() == 42);

    devmode::core::DevJsonStore::instance().flush_all();
    fs::remove_all(manifest_path.parent_path());
}

TEST_CASE("ManifestStore edits survive reloads") {
    nlohmann::json initial = {
        {"assets", {
            {"TestAsset", {
                {"value", 7}
            }}
        }},
        {"maps", nlohmann::json::object()},
        {"rooms", nlohmann::json::array()}
    };

    const auto manifest_path = make_manifest_path("survive_reload", initial);

    int load_calls = 0;
    auto loader = [&]() {
        ++load_calls;
        return load_manifest_from_path(manifest_path);
    };

    devmode::core::ManifestStore store(manifest_path, loader);
    CHECK(load_calls == 0);

    auto view = store.get_asset("TestAsset");
    CHECK(load_calls == 1);
    REQUIRE(view);
    CHECK(view->at("value").get<int>() == 7);

    auto session = store.begin_asset_edit("TestAsset");
    REQUIRE(session);
    session.data()["value"] = 99;
    CHECK(session.commit());
    store.flush();

    devmode::core::ManifestStore reloaded(manifest_path, loader);
    CHECK(load_calls == 1);

    auto reloaded_view = reloaded.get_asset("TestAsset");
    CHECK(load_calls == 2);
    REQUIRE(reloaded_view);
    CHECK(reloaded_view->at("value").get<int>() == 99);

    devmode::core::DevJsonStore::instance().flush_all();
    fs::remove_all(manifest_path.parent_path());
}

TEST_CASE("ManifestStore create and remove assets updates manifest entries") {
    nlohmann::json initial = {
        {"assets", nlohmann::json::object()},
        {"maps", nlohmann::json::object()},
        {"rooms", nlohmann::json::array()}
    };

    const auto manifest_path = make_manifest_path("create_remove", initial);

    auto loader = [manifest_path]() {
        return load_manifest_from_path(manifest_path);
    };

    devmode::core::ManifestStore store(manifest_path, loader);

    auto session = store.begin_asset_edit("NewAsset", true);
    REQUIRE(session);
    CHECK(session.is_new_asset());
    session.data() = {
        {"asset_name", "NewAsset"},
        {"asset_type", "Object"},
        {"animations", nlohmann::json::object()},
        {"start", ""}
    };
    CHECK(session.commit());
    store.flush();

    auto manifest_after_create = read_json(manifest_path);
    REQUIRE(manifest_after_create.contains("assets"));
    CHECK(manifest_after_create["assets"].contains("NewAsset"));

    CHECK(store.remove_asset("NewAsset"));
    store.flush();

    auto manifest_after_delete = read_json(manifest_path);
    REQUIRE(manifest_after_delete.contains("assets"));
    CHECK(!manifest_after_delete["assets"].contains("NewAsset"));

    devmode::core::DevJsonStore::instance().flush_all();
    fs::remove_all(manifest_path.parent_path());
}

TEST_CASE("Manifest spawn-group cleanup updates manifest without touching legacy files") {
    nlohmann::json initial = {
        {"assets", {
            {"KeepAsset", {
                {"spawn_groups", nlohmann::json::array({
                    nlohmann::json{{"spawn_id", "group"},
                                    {"display_name", "Group"},
                                    {"priority", 0},
                                    {"candidates", nlohmann::json::array({
                                        nlohmann::json{{"name", "ObsoleteAsset"}, {"chance", 50}},
                                        nlohmann::json{{"name", "Other"}, {"chance", 50}}
                                    })}}
                })}
            }},
            {"ObsoleteAsset", {
                {"spawn_groups", nlohmann::json::array()}
            }}
        }},
        {"maps", {
            {"map-1", nlohmann::json{
                {"layers", nlohmann::json::array({
                    nlohmann::json{{"candidates", nlohmann::json::array({
                        nlohmann::json{{"name", "ObsoleteAsset"}},
                        nlohmann::json{{"name", "Other"}}
                    })}}
                })}
            }}
        }},
        {"rooms", nlohmann::json::array()}
    };

    const auto manifest_path = make_manifest_path("spawn_group_cleanup", initial);
    const fs::path root = manifest_path.parent_path();

    const fs::path map_info_path = root / "MAPS" / "Example" / "map_info.json";
    fs::create_directories(map_info_path.parent_path());
    {
        std::ofstream out(map_info_path);
        out << R"({"sentinel":"map"})";
    }

    const fs::path asset_info_path = root / "SRC" / "KeepAsset" / "info.json";
    fs::create_directories(asset_info_path.parent_path());
    {
        std::ofstream out(asset_info_path);
        out << R"({"sentinel":"asset"})";
    }

    const auto map_info_time = fs::last_write_time(map_info_path);
    const auto asset_info_time = fs::last_write_time(asset_info_path);

    auto loader = [manifest_path]() { return load_manifest_from_path(manifest_path); };
    devmode::core::ManifestStore store(manifest_path, loader);

    bool flush_required = false;
    CHECK(store.remove_asset("ObsoleteAsset"));
    flush_required = true;

    const nlohmann::json& manifest = store.manifest_json();

    auto maps_it = manifest.find("maps");
    if (maps_it != manifest.end() && maps_it->is_object()) {
        for (auto it = maps_it->begin(); it != maps_it->end(); ++it) {
            nlohmann::json map_entry = *it;
            if (devmode::manifest_utils::remove_asset_from_spawn_groups(map_entry, "ObsoleteAsset")) {
                CHECK(store.update_map_entry(it.key(), map_entry));
                flush_required = true;
            }
        }
    }

    auto assets_it = manifest.find("assets");
    if (assets_it != manifest.end() && assets_it->is_object()) {
        for (auto it = assets_it->begin(); it != assets_it->end(); ++it) {
            const std::string& name = it.key();
            if (name == "ObsoleteAsset") {
                continue;
            }
            auto transaction = store.begin_asset_transaction(name);
            REQUIRE(transaction);
            if (devmode::manifest_utils::remove_asset_from_spawn_groups(transaction.data(), "ObsoleteAsset")) {
                CHECK(transaction.finalize());
                flush_required = true;
            }
        }
    }

    if (flush_required) {
        store.flush();
    }

    const auto persisted = read_json(manifest_path);
    const auto& map_entry = persisted["maps"]["map-1"];
    REQUIRE(map_entry.is_object());
    REQUIRE(map_entry.contains("layers"));
    auto layers = map_entry["layers"];
    REQUIRE(layers.is_array());
    REQUIRE_FALSE(layers.empty());
    auto candidates = layers.front()["candidates"];
    REQUIRE(candidates.is_array());
    CHECK(std::none_of(candidates.begin(), candidates.end(), [](const nlohmann::json& candidate) {
        return candidate.is_object() && candidate.value("name", std::string{}) == "ObsoleteAsset";
    }));

    const auto& keep_asset = persisted["assets"]["KeepAsset"];
    REQUIRE(keep_asset.is_object());
    auto groups = keep_asset.value("spawn_groups", nlohmann::json::array());
    REQUIRE(groups.is_array());
    REQUIRE_FALSE(groups.empty());
    auto keep_candidates = groups.front().value("candidates", nlohmann::json::array());
    REQUIRE(keep_candidates.is_array());
    CHECK(std::none_of(keep_candidates.begin(), keep_candidates.end(), [](const nlohmann::json& candidate) {
        return candidate.is_object() && candidate.value("name", std::string{}) == "ObsoleteAsset";
    }));

    CHECK(fs::exists(map_info_path));
    CHECK(fs::exists(asset_info_path));
    CHECK(fs::last_write_time(map_info_path) == map_info_time);
    CHECK(fs::last_write_time(asset_info_path) == asset_info_time);

    devmode::core::DevJsonStore::instance().flush_all();
    fs::remove_all(manifest_path.parent_path());
}

