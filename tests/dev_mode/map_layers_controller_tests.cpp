#define DOCTEST_CONFIG_IMPLEMENT_WITHOUT_MAIN
#include "doctest/doctest.h"

#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include "dev_mode/core/manifest_store.hpp"
#include "dev_mode/map_layers_controller.hpp"

namespace {
namespace fs = std::filesystem;

nlohmann::json load_json(const fs::path& path) {
    std::ifstream in(path);
    nlohmann::json data = nlohmann::json::object();
    if (in.is_open()) {
        in >> data;
    }
    return data;
}

fs::path make_manifest(const std::string& test_name, const nlohmann::json& manifest) {
    fs::path root = fs::temp_directory_path() / "vibble_map_layers_controller_tests" / test_name;
    fs::remove_all(root);
    fs::create_directories(root);
    fs::path manifest_path = root / "manifest.json";
    std::ofstream out(manifest_path);
    out << manifest.dump(2);
    return manifest_path;
}

manifest::ManifestData manifest_loader_from(const fs::path& manifest_path) {
    manifest::ManifestData data;
    data.raw = load_json(manifest_path);
    data.assets = data.raw.value("assets", nlohmann::json::object());
    data.maps = data.raw.value("maps", nlohmann::json::object());
    data.rooms = data.raw.value("rooms", nlohmann::json::array());
    return data;
}

nlohmann::json make_map_info(const std::string& room_name) {
    return nlohmann::json{
        {"map_layers", nlohmann::json::array({nlohmann::json{
            {"level", 0},
            {"name", "Layer 0"},
            {"min_rooms", 0},
            {"max_rooms", 0},
            {"rooms", nlohmann::json::array({nlohmann::json{
                {"name", room_name},
                {"min_instances", 0},
                {"max_instances", 0},
                {"weight", 1},
                {"children", nlohmann::json::array()},
                {"required_children", nlohmann::json::array()}
            }})}
        }})},
        {"rooms_data", nlohmann::json::object({
            {room_name, nlohmann::json::object({
                {"is_spawn", false},
                {"radius", 0},
                {"min_width", 0},
                {"max_width", 0},
                {"min_height", 0},
                {"max_height", 0}
            })}
        })}
    };
}

} // namespace

TEST_CASE("MapLayersController reloads map info from manifest") {
    const std::string map_id = "MAP_FROM_MANIFEST";
    const std::string manifest_room = "ManifestRoom";

    nlohmann::json manifest_entry = make_map_info(manifest_room);
    nlohmann::json manifest_payload = {
        {"assets", nlohmann::json::object()},
        {"maps", nlohmann::json::object({{map_id, manifest_entry}})},
        {"rooms", nlohmann::json::array()}
    };

    const fs::path manifest_path = make_manifest("controller_reload", manifest_payload);
    auto loader = [manifest_path]() { return manifest_loader_from(manifest_path); };
    devmode::core::ManifestStore store(manifest_path, loader);

    nlohmann::json map_info = make_map_info("InitialRoom");
    MapLayersController controller;
    controller.bind(&map_info, manifest_path.parent_path().string());
    controller.set_manifest_store(&store, map_id);

    CHECK(controller.reload());

    CHECK(map_info == manifest_entry);

    auto rooms = controller.available_rooms();
    REQUIRE(rooms.size() == 1);
    CHECK(rooms.front() == manifest_room);

    fs::remove_all(manifest_path.parent_path());
}

TEST_CASE("MapLayersController refuses to save without manifest store") {
    nlohmann::json map_info = make_map_info("LonelyRoom");
    MapLayersController controller;
    controller.bind(&map_info, "");

    map_info["map_layers"][0]["rooms"][0]["max_instances"] = 5;
    CHECK_FALSE(controller.save());
}

TEST_CASE("MapLayersController refuses to reload without manifest store") {
    nlohmann::json map_info = make_map_info("LonelyRoom");
    MapLayersController controller;
    controller.bind(&map_info, "");

    CHECK_FALSE(controller.reload());
}
