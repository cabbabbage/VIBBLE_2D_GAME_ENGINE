#include "doctest/doctest.h"

#include <filesystem>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include "dev_mode/core/manifest_store.hpp"
#include "dev_mode/dev_controls_persistence.hpp"
#include "core/manifest/manifest_loader.hpp"

namespace {

devmode::core::ManifestStore make_store(const std::filesystem::path& manifest_path,
                                        nlohmann::json manifest,
                                        nlohmann::json& captured_submission) {
    manifest::ManifestData data;
    data.raw = manifest;
    data.assets = manifest.value("assets", nlohmann::json::object());
    data.maps = manifest.value("maps", nlohmann::json::object());
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

} // namespace

TEST_CASE("persist_map_manifest_entry rejects empty identifiers") {
    namespace fs = std::filesystem;
    const fs::path manifest_path = fs::temp_directory_path() / "manifest_empty_id.json";
    nlohmann::json manifest = {
        {"assets", nlohmann::json::object()},
        {"maps", nlohmann::json::object()}
    };
    nlohmann::json submitted;
    auto store = make_store(manifest_path, manifest, submitted);

    std::ostringstream log;
    CHECK_FALSE(devmode::persist_map_manifest_entry(store, "", nlohmann::json::object(), log));
    CHECK_FALSE(submitted.is_object());
    CHECK(log.str().find("Map identifier is empty") != std::string::npos);
}

TEST_CASE("persist_map_manifest_entry updates manifest store entry") {
    namespace fs = std::filesystem;
    const fs::path manifest_path = fs::temp_directory_path() / "manifest_update_map.json";
    nlohmann::json manifest = {
        {"assets", nlohmann::json::object()},
        {"maps", {{"FORREST", {{"name", "FORREST"}}}}},
        {"version", 1}
    };
    nlohmann::json submitted;
    auto store = make_store(manifest_path, manifest, submitted);

    nlohmann::json payload = {
        {"name", "FORREST"},
        {"version", 2}
    };

    std::ostringstream log;
    CHECK(devmode::persist_map_manifest_entry(store, "FORREST", payload, log));
    CHECK(submitted.is_object());
    CHECK(submitted["maps"].is_object());
    CHECK(submitted["maps"]["FORREST"] == payload);
    const auto& manifest_view = store.manifest_json();
    REQUIRE(manifest_view.contains("maps"));
    CHECK(manifest_view["maps"]["FORREST"] == payload);
    CHECK(log.str().empty());
}
