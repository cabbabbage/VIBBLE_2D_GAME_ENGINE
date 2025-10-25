#include "doctest/doctest.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "asset/asset_info.hpp"
#include "dev_mode/asset_sections/Section_SpawnGroups.hpp"
#include "dev_mode/core/manifest_store.hpp"
#include "dev_mode/core/dev_json_store.hpp"

struct SectionSpawnGroupsTestAccess {
    static void reload(Section_SpawnGroups& section) { section.reload_from_file(); }
    static void set_rebuilding(Section_SpawnGroups& section, bool value) { section.rebuilding_ = value; }
    static void add_spawn_group(Section_SpawnGroups& section) { section.add_spawn_group(); }
    static void reorder_spawn_group(Section_SpawnGroups& section, const std::string& id, size_t new_index) {
        section.reorder_spawn_group(id, new_index);
    }
    static void delete_spawn_group(Section_SpawnGroups& section, const std::string& id) {
        section.delete_spawn_group(id);
    }
};

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
    return data;
}

nlohmann::json basic_asset_metadata() {
    return {
        {"asset_name", "TestAsset"},
        {"asset_type", "Object"}
    };
}

} // namespace

TEST_CASE("Section_SpawnGroups mutates manifest spawn_groups") {
    nlohmann::json manifest = {
        {"assets", {
            {"TestAsset", {
                {"spawn_groups", nlohmann::json::array()}
            }}
        }},
        {"maps", nlohmann::json::object()}
    };

    auto& initial_groups = manifest["assets"]["TestAsset"]["spawn_groups"];
    initial_groups.push_back({
        {"spawn_id", "spn-one"},
        {"display_name", "One"},
        {"priority", 0},
        {"candidates", nlohmann::json::array({
            nlohmann::json{{"name", "alpha"}, {"chance", 100}}
        })}
    });
    initial_groups.push_back({
        {"spawn_id", "spn-two"},
        {"display_name", "Two"},
        {"priority", 1},
        {"candidates", nlohmann::json::array({
            nlohmann::json{{"name", "beta"}, {"chance", 100}}
        })}
    });

    const auto manifest_path = make_manifest_path("spawn_group_edits", manifest);
    auto loader = [manifest_path]() { return load_manifest_from_path(manifest_path); };
    devmode::core::ManifestStore store(manifest_path, loader);

    Section_SpawnGroups section;
    section.set_manifest_store(&store);
    SectionSpawnGroupsTestAccess::set_rebuilding(section, true);

    std::vector<nlohmann::json> notifications;
    std::vector<std::string> removed_ids;
    section.set_spawn_config_listener([&](const nlohmann::json& entry) { notifications.push_back(entry); });
    section.set_spawn_group_removed_listener([&](const std::string& id) { removed_ids.push_back(id); });

    auto metadata = basic_asset_metadata();
    metadata["spawn_groups"] = manifest["assets"]["TestAsset"]["spawn_groups"];
    auto info = std::make_shared<AssetInfo>("TestAsset", metadata);
    section.set_info(info);
    SectionSpawnGroupsTestAccess::reload(section);

    REQUIRE(section.groups().is_array());
    CHECK(section.groups().size() == 2);

    notifications.clear();
    removed_ids.clear();

    SectionSpawnGroupsTestAccess::add_spawn_group(section);
    store.flush();
    auto after_add = read_json(manifest_path);
    auto& stored_groups = after_add["assets"]["TestAsset"]["spawn_groups"];
    REQUIRE(stored_groups.is_array());
    CHECK(stored_groups.size() == 3);
    for (std::size_t i = 0; i < stored_groups.size(); ++i) {
        if (stored_groups[i].is_object()) {
            CHECK(stored_groups[i]["priority"].get<int>() == static_cast<int>(i));
        }
    }
    REQUIRE_FALSE(notifications.empty());
    const std::string added_id = notifications.back().value("spawn_id", std::string{});
    CHECK_FALSE(added_id.empty());
    CHECK(removed_ids.empty());

    notifications.clear();
    removed_ids.clear();
    SectionSpawnGroupsTestAccess::reorder_spawn_group(section, "spn-two", 0);
    store.flush();
    auto after_reorder = read_json(manifest_path);
    auto reordered = after_reorder["assets"]["TestAsset"]["spawn_groups"];
    REQUIRE(reordered.is_array());
    CHECK(reordered.front().value("spawn_id", std::string{}) == "spn-two");
    for (std::size_t i = 0; i < reordered.size(); ++i) {
        if (reordered[i].is_object()) {
            CHECK(reordered[i]["priority"].get<int>() == static_cast<int>(i));
        }
    }
    CHECK_FALSE(notifications.empty());
    CHECK(notifications.back().value("spawn_id", std::string{}) == "spn-two");
    CHECK(removed_ids.empty());

    removed_ids.clear();
    SectionSpawnGroupsTestAccess::delete_spawn_group(section, added_id);
    store.flush();
    auto after_delete = read_json(manifest_path);
    auto deleted = after_delete["assets"]["TestAsset"]["spawn_groups"];
    CHECK(deleted.size() == 2);
    CHECK_FALSE(std::any_of(deleted.begin(), deleted.end(), [&](const nlohmann::json& entry) {
        return entry.is_object() && entry.value("spawn_id", std::string{}) == added_id;
    }));
    REQUIRE_FALSE(removed_ids.empty());
    CHECK(removed_ids.back() == added_id);

    devmode::core::DevJsonStore::instance().flush_all();
    fs::remove_all(manifest_path.parent_path());
}

