#include "doctest/doctest.h"

#include "dev_mode/dev_controls.hpp"

#include <SDL.h>
#include <nlohmann/json.hpp>

TEST_CASE("RoomAreaCache caches polygons until invalidated") {
    DevControls::RoomAreaCache cache;

    std::size_t callback_count = 0;
    std::size_t observed_generation = 0;
    std::size_t observed_polygon_count = 0;
    cache.set_listener([&](const DevControls::RoomAreaCache::PolygonList& polys, std::size_t generation) {
        ++callback_count;
        observed_generation = generation;
        observed_polygon_count = polys.size();
    });

    nlohmann::json room_json;
    room_json["areas"] = nlohmann::json::array({
        nlohmann::json{
            {"name", "start"},
            {"type", "trigger"},
            {"kind", "Trigger"},
            {"anchor", nlohmann::json{{"x", 2}, {"y", 1}}},
            {"points", nlohmann::json::array({
                nlohmann::json{{"x", 0}, {"y", 0}},
                nlohmann::json{{"x", 2}, {"y", 0}},
                nlohmann::json{{"x", 1}, {"y", 2}}
            })}
        }
    });

    const auto& first = cache.ensure_from_json(&room_json);
    REQUIRE_EQ(first.size(), 1);
    CHECK_EQ(first.front().first, std::string("trigger"));
    CHECK_EQ(first.front().second.size(), 3);
    CHECK_EQ(callback_count, 1);
    CHECK_EQ(observed_generation, 1);
    CHECK_EQ(observed_polygon_count, 1);

    // Mutate JSON without invalidating: cache should not rebuild yet.
    room_json["areas"][0]["points"].push_back(nlohmann::json{{"x", 3}, {"y", 1}});
    const auto& second = cache.ensure_from_json(&room_json);
    CHECK_EQ(&second, &first); // Same cached container reused
    CHECK_EQ(second.front().second.size(), 3);
    CHECK_EQ(callback_count, 1);
    CHECK_EQ(observed_generation, 1);

    cache.invalidate();
    const auto& third = cache.ensure_from_json(&room_json);
    CHECK_EQ(&third, &first);
    CHECK_EQ(callback_count, 2);
    CHECK_EQ(observed_generation, 2);
    CHECK_EQ(observed_polygon_count, 1);
    CHECK_EQ(third.front().second.size(), 4);

    // Existing references should now observe the updated polygon list.
    CHECK_EQ(first.front().second.size(), 4);
}
