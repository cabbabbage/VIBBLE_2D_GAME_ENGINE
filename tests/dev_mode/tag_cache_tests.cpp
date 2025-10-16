#define DOCTEST_CONFIG_IMPLEMENT_WITHOUT_MAIN
#include "doctest/doctest.h"

#include "asset/asset_info.hpp"
#include "dev_mode/search_assets.hpp"
#include "dev_mode/core/manifest_store.hpp"
#include "dev_mode/tag_utils.hpp"
#include "core/manifest/manifest_loader.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <random>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

struct ScopedTestAsset {
    ScopedTestAsset(const std::string& base_name, const nlohmann::json& payload) {
        static int counter = 0;
        name = base_name + "_" + std::to_string(++counter);
        dir = fs::path("SRC") / name;
        fs::create_directories(dir);
        std::ofstream out(dir / "info.json");
        out << payload.dump(4);
    }

    ~ScopedTestAsset() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    const std::string& asset_name() const { return name; }

    fs::path dir;
    std::string name;
};

} // namespace

TEST_CASE("AssetInfo tag caches stay synchronized with mutations") {
    const nlohmann::json payload = {
        {"asset_type", "object"},
        {"start", "default"},
        {"animations", nlohmann::json::object()},
        {"tags", nlohmann::json::array({"passable", "decor"})},
        {"anti_tags", nlohmann::json::array({"hazard"})}
    };

    ScopedTestAsset scoped("tag_cache", payload);
    AssetInfo info(scoped.asset_name());

    REQUIRE(info.has_tag("passable"));
    REQUIRE(info.has_tag("decor"));
    CHECK(info.tag_lookup().count("passable") == 1);
    CHECK(info.tag_lookup().count("decor") == 1);
    CHECK(info.anti_tag_lookup().count("hazard") == 1);

    info.add_tag("collectible");
    CHECK(info.has_tag("collectible"));
    CHECK_EQ(info.tags.back(), std::string{"collectible"});

    info.remove_tag("decor");
    CHECK_FALSE(info.has_tag("decor"));
    CHECK(info.tag_lookup().count("decor") == 0);

    info.set_tags({"alpha", "beta", "gamma"});
    CHECK(info.has_tag("alpha"));
    CHECK(info.has_tag("beta"));
    CHECK(info.has_tag("gamma"));
    CHECK_EQ(info.tags.front(), std::string{"alpha"});

    info.set_anti_tags({"lava"});
    CHECK(info.anti_tag_lookup().count("lava") == 1);

    info.add_anti_tag("spikes");
    CHECK(info.anti_tag_lookup().count("spikes") == 1);

    info.remove_anti_tag("lava");
    CHECK(info.anti_tag_lookup().count("lava") == 0);
}

TEST_CASE("AssetInfo tag cache outperforms linear scans on large pools") {
    const std::size_t tag_count = 3072;
    const std::size_t query_count = 20000;

    std::vector<std::string> baseline_tags;
    baseline_tags.reserve(tag_count);
    for (std::size_t i = 0; i < tag_count; ++i) {
        baseline_tags.push_back("tag_" + std::to_string(i));
    }

    nlohmann::json payload = {
        {"asset_type", "object"},
        {"start", "default"},
        {"animations", nlohmann::json::object()},
        {"tags", baseline_tags}
    };

    ScopedTestAsset scoped("tag_perf", payload);
    AssetInfo info(scoped.asset_name());
    info.set_tags(baseline_tags);

    std::vector<std::string> queries;
    queries.reserve(query_count);
    std::mt19937 rng(1337);
    std::uniform_int_distribution<std::size_t> dist(0, tag_count - 1);
    for (std::size_t i = 0; i < query_count; ++i) {
        if ((i % 5) == 0) {
            queries.push_back("missing_" + std::to_string(i));
        } else {
            queries.push_back(baseline_tags[dist(rng)]);
        }
    }

    const auto& tag_vector = info.tags;

    auto linear_start = std::chrono::steady_clock::now();
    std::size_t linear_hits = 0;
    for (const auto& query : queries) {
        if (std::find(tag_vector.begin(), tag_vector.end(), query) != tag_vector.end()) {
            ++linear_hits;
        }
    }
    auto linear_elapsed = std::chrono::steady_clock::now() - linear_start;

    auto cached_start = std::chrono::steady_clock::now();
    std::size_t cached_hits = 0;
    for (const auto& query : queries) {
        if (info.has_tag(query)) {
            ++cached_hits;
        }
    }
    auto cached_elapsed = std::chrono::steady_clock::now() - cached_start;

    CHECK_EQ(linear_hits, cached_hits);

    const auto linear_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(linear_elapsed).count();
    const auto cached_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(cached_elapsed).count();

    CAPTURE(linear_ns);
    CAPTURE(cached_ns);

    CHECK_LT(cached_ns, linear_ns);
}

TEST_CASE("SearchAssets reflects manifest tag mutations") {
    using devmode::core::ManifestStore;

    nlohmann::json manifest_json = {
        {"assets",
            {
                {
                    "Alpha",
                    {
                        {"asset_name", "Alpha"},
                        {"tags", nlohmann::json::array({"decor"})}
                    }
                }
            }
        },
        {"maps", nlohmann::json::object()}
    };

    auto loader = [&]() {
        manifest::ManifestData data;
        data.raw = manifest_json;
        if (manifest_json.contains("assets")) data.assets = manifest_json["assets"];
        if (manifest_json.contains("maps")) data.maps = manifest_json["maps"];
        return data;
    };

    std::filesystem::path manifest_path = std::filesystem::temp_directory_path() / "search_assets_manifest_test.json";
    ManifestStore store(manifest_path, loader, [](auto&&...) {}, []() {}, 2);

    SearchAssets search(&store);
    search.set_embedded_mode(true);
    search.open([](const std::string&) {});

    search.set_query_for_testing("decor");
    auto results = search.results_for_testing();
    CHECK(std::find(results.begin(), results.end(), std::make_pair(std::string{"decor"}, true)) != results.end());

    manifest_json["assets"]["Alpha"]["tags"] = nlohmann::json::array({"collectible"});
    tag_utils::notify_tags_changed();

    search.set_query_for_testing("decor");
    results = search.results_for_testing();
    CHECK(std::find(results.begin(), results.end(), std::make_pair(std::string{"decor"}, true)) == results.end());

    search.set_query_for_testing("collectible");
    results = search.results_for_testing();
    CHECK(std::find(results.begin(), results.end(), std::make_pair(std::string{"collectible"}, true)) != results.end());
}

