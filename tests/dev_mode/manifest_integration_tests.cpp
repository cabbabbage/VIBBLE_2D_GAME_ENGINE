#define DOCTEST_CONFIG_IMPLEMENT_WITHOUT_MAIN
#include "doctest/doctest.h"

#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include "dev_mode/core/manifest_store.hpp"
#include "dev_mode/core/dev_json_store.hpp"
#include "dev_mode/asset_sections/animation_editor_window/AnimationDocument.hpp"
#include "dev_mode/asset_sections/animation_editor_window/CustomControllerService.hpp"
#include "core/manifest/manifest_loader.hpp"

namespace {
namespace fs = std::filesystem;

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

fs::path make_temp_manifest(const std::string& test_name, const nlohmann::json& payload) {
    fs::path root = fs::temp_directory_path() / "vibble_manifest_integration_tests" / test_name;
    fs::remove_all(root);
    fs::create_directories(root);
    fs::path manifest_path = root / "manifest.json";
    std::ofstream out(manifest_path);
    out << payload.dump(2);
    return manifest_path;
}

} // namespace

TEST_CASE("AnimationDocument saves manifest payloads") {
    nlohmann::json manifest = {
        {"assets", {
            {"TestAsset", {
                {"animations", {
                    {"idle", {
                        {"source", {{"kind", "folder"}, {"path", "idle"}}},
                        {"number_of_frames", 1},
                        {"movement", nlohmann::json::array({nlohmann::json::array({0, 0})})}
                    }}
                }},
                {"start", "idle"}
            }}
        }},
        {"maps", nlohmann::json::object()},
        {"rooms", nlohmann::json::array()}
    };

    const fs::path manifest_path = make_temp_manifest("animation_document", manifest);
    auto loader = [manifest_path]() { return load_manifest_from_path(manifest_path); };

    devmode::core::ManifestStore store(manifest_path, loader);
    auto transaction = store.begin_asset_transaction("TestAsset");
    REQUIRE(transaction);

    animation_editor::AnimationDocument document;
    bool save_called = false;
    fs::path asset_root = manifest_path.parent_path() / "SRC" / "TestAsset";
    document.load_from_manifest(transaction.data(), asset_root, [&](const nlohmann::json& payload) {
        transaction.data() = payload;
        CHECK(transaction.save());
        save_called = true;
    });

    document.create_animation("run");
    document.save_to_file();
    CHECK(save_called);
    CHECK(transaction.finalize());

    store.flush();
    devmode::core::DevJsonStore::instance().flush_all();

    nlohmann::json updated = read_json(manifest_path);
    auto& asset = updated["assets"]["TestAsset"];
    REQUIRE(asset.is_object());
    auto animations = asset["animations"];
    REQUIRE(animations.is_object());
    CHECK(animations.contains("run"));

    fs::remove_all(manifest_path.parent_path());
}

TEST_CASE("CustomControllerService updates manifest metadata") {
    nlohmann::json manifest = {
        {"assets", {
            {"TestAsset", {
                {"animations", {
                    {"idle", {
                        {"source", {{"kind", "folder"}, {"path", "idle"}}},
                        {"number_of_frames", 1},
                        {"movement", nlohmann::json::array({nlohmann::json::array({0, 0})})}
                    }}
                }},
                {"start", "idle"}
            }}
        }},
        {"maps", nlohmann::json::object()},
        {"rooms", nlohmann::json::array()}
    };

    const fs::path manifest_path = make_temp_manifest("controller_service", manifest);
    auto loader = [manifest_path]() { return load_manifest_from_path(manifest_path); };
    devmode::core::ManifestStore store(manifest_path, loader);

    fs::path project_root = manifest_path.parent_path();
    fs::path engine_dir = project_root / "ENGINE";
    fs::path controller_dir = engine_dir / "custom_controllers";
    fs::create_directories(controller_dir);
    fs::create_directories(engine_dir / "asset");
    fs::path asset_root = project_root / "SRC" / "TestAsset";
    fs::create_directories(asset_root);

    animation_editor::CustomControllerService service;
    service.set_manifest_store(&store);
    service.set_manifest_asset_key("TestAsset");
    service.set_asset_root(asset_root);

    service.create_new_controller("test_controller");
    service.register_controller_with_animation("test_controller", "idle");

    store.flush();
    devmode::core::DevJsonStore::instance().flush_all();

    nlohmann::json updated = read_json(manifest_path);
    auto& asset = updated["assets"]["TestAsset"];
    REQUIRE(asset.is_object());
    CHECK(asset.value("custom_controller_key", "") == "test_controller");

    auto animations = asset["animations"];
    REQUIRE(animations.is_object());
    auto entry = animations["idle"];
    REQUIRE(entry.is_object());
    CHECK(entry.value("custom_animation_controller_key", "") == "test_controller");
    CHECK(entry.value("has_custom_animation_controller", false));

    fs::remove_all(manifest_path.parent_path());
}

