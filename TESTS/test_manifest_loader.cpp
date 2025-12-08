#include "doctest/doctest.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <thread>
#include <chrono>

#include "core/manifest/manifest_loader.hpp"
#include "dev_mode/core/manifest_store.hpp"
#include "dev_mode/manifest_asset_utils.hpp"
#include "utils/log.hpp"

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

static fs::path test_root() {
#ifdef PROJECT_ROOT
    return fs::path(PROJECT_ROOT);
#else
    return fs::current_path() / "TEST_TMP";
#endif
}

TEST_CASE("manifest loader retries and falls back to cached on parse error") {
    vibble::log::set_level(vibble::log::Level::Warn);

    const fs::path root = test_root();
    const fs::path manifest = root / "manifest.json";
    std::error_code ec;
    fs::create_directories(root, ec);
    // Start fresh
    fs::remove(manifest, ec);

    // First load should create default manifest
    auto data1 = manifest::load_manifest();
    REQUIRE(data1.raw.is_object());
    REQUIRE(data1.raw["version"].is_number());
    const auto version1 = data1.raw["version"].get<int>();

    // Corrupt the manifest on disk
    {
        std::ofstream out(manifest);
        REQUIRE(out.is_open());
        out << "{\n\n"; // invalid JSON
    }

    // Next load should not throw; should fall back to cached data1
    auto start = std::chrono::steady_clock::now();
    auto data2 = manifest::load_manifest();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    // There should have been a brief retry delay (~50ms)
    CHECK(elapsed.count() >= 40);

    CHECK(data2.raw.is_object());
    CHECK(data2.raw["version"].get<int>() == version1);

    // Fix the manifest; next load should reflect the new content
    nlohmann::json fixed = nlohmann::json::object();
    fixed["version"] = version1 + 1;
    fixed["assets"] = nlohmann::json::object();
    fixed["maps"] = nlohmann::json::object();
    {
        std::ofstream out(manifest);
        REQUIRE(out.is_open());
        out << fixed.dump(2);
    }

    auto data3 = manifest::load_manifest();
    CHECK(data3.raw["version"].get<int>() == version1 + 1);
}

TEST_CASE("manifest store helper removes asset entries") {
    vibble::log::set_level(vibble::log::Level::Warn);

    const fs::path root = test_root() / "manifest_remove_helper";
    const fs::path manifest_path = root / "manifest.json";
    std::error_code ec;
    fs::create_directories(root, ec);

    nlohmann::json initial = nlohmann::json::object();
    initial["version"] = 1;
    initial["assets"] = {
        {"Alpha", {
            {"asset_name", "Alpha"},
            {"asset_directory", "SRC/assets/Alpha"},
            {"asset_type", "Object"}
        }}
    };
    initial["maps"] = nlohmann::json::object();

    {
        std::ofstream out(manifest_path);
        REQUIRE(out.is_open());
        out << initial.dump(2);
    }

    auto loader = [&]() {
        manifest::ManifestData data;
        std::ifstream in(manifest_path);
        REQUIRE(in.is_open());
        in >> data.raw;
        if (!data.raw.is_object()) {
            data.raw = nlohmann::json::object();
        }
        if (!data.raw.contains("assets")) {
            data.raw["assets"] = nlohmann::json::object();
        }
        if (!data.raw.contains("maps")) {
            data.raw["maps"] = nlohmann::json::object();
        }
        data.assets = data.raw["assets"];
        data.maps = data.raw["maps"];
        return data;
    };

    auto submit = [&](const fs::path&, const nlohmann::json& payload, int indent) {
        std::ofstream out(manifest_path);
        REQUIRE(out.is_open());
        out << payload.dump(indent);
    };

    bool flushed = false;
    auto flush = [&]() { flushed = true; };

    devmode::core::ManifestStore store(manifest_path, loader, submit, flush, 2);
    REQUIRE(store.resolve_asset_name("Alpha").has_value());

    const auto result = devmode::manifest_utils::remove_asset_entry(&store, "Alpha");
    CHECK(result.removed);
    CHECK(result.used_store);
    CHECK_FALSE(store.resolve_asset_name("Alpha").has_value());
    CHECK(store.dirty());

    store.flush();
    CHECK(flushed);

    nlohmann::json written;
    {
        std::ifstream in(manifest_path);
        REQUIRE(in.is_open());
        in >> written;
    }
    CHECK(written["assets"].is_object());
    CHECK(written["assets"].empty());
}
