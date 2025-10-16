#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include "dev_mode/core/manifest_store.hpp"
#include "dev_mode/core/dev_json_store.hpp"

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

