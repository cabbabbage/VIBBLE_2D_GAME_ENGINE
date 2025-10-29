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

    const SDL_Point room_center{10, 20};
    const auto& first = cache.ensure_from_json(&room_json, room_center);
    REQUIRE_EQ(first.size(), 1);
    CHECK_EQ(first.front().type, std::string("trigger"));
    CHECK_EQ(first.front().points.size(), 3);
    CHECK_EQ(first.front().anchor.x, room_center.x);
    CHECK_EQ(first.front().anchor.y, room_center.y);
    CHECK_EQ(first.front().points[0].x, room_center.x);
    CHECK_EQ(first.front().points[0].y, room_center.y);
    CHECK_EQ(callback_count, 1);
    CHECK_EQ(observed_generation, 1);
    CHECK_EQ(observed_polygon_count, 1);

    // Mutate JSON without invalidating: cache should not rebuild yet.
    room_json["areas"][0]["points"].push_back(nlohmann::json{{"x", 3}, {"y", 1}});
    const auto& second = cache.ensure_from_json(&room_json, room_center);
    CHECK_EQ(&second, &first); // Same cached container reused
    CHECK_EQ(second.front().points.size(), 3);
    CHECK_EQ(callback_count, 1);
    CHECK_EQ(observed_generation, 1);

    cache.invalidate();
    const auto& third = cache.ensure_from_json(&room_json, room_center);
    CHECK_EQ(&third, &first);
    CHECK_EQ(callback_count, 2);
    CHECK_EQ(observed_generation, 2);
    CHECK_EQ(observed_polygon_count, 1);
    CHECK_EQ(third.front().points.size(), 4);

    // Existing references should now observe the updated polygon list.
    CHECK_EQ(first.front().points.size(), 4);
}

TEST_CASE("RoomAreaCache resolves room-centered anchors") {
    DevControls::RoomAreaCache cache;

    nlohmann::json room_json;
    room_json["areas"] = nlohmann::json::array({
        nlohmann::json{
            {"name", "spawn_a"},
            {"type", "spawn"},
            {"kind", "Spawn"},
            {"anchor", nlohmann::json{{"x", 12}, {"y", -8}}},
            {"anchor_relative_to_center", true},
            {"points", nlohmann::json::array({
                nlohmann::json{{"x", -10}, {"y", 0}},
                nlohmann::json{{"x", 0}, {"y", 20}},
                nlohmann::json{{"x", 10}, {"y", 0}}
            })}
        }
    });

    const SDL_Point room_center{100, 200};
    const auto& polygons = cache.ensure_from_json(&room_json, room_center);
    REQUIRE_EQ(polygons.size(), 1);
    const auto& poly = polygons.front();
    CHECK_EQ(poly.anchor.x, room_center.x + 12);
    CHECK_EQ(poly.anchor.y, room_center.y - 8);
    REQUIRE_EQ(poly.points.size(), 3);
    CHECK_EQ(poly.points[0].x, room_center.x + 2);
    CHECK_EQ(poly.points[0].y, room_center.y - 8);
    CHECK_EQ(poly.points[1].x, room_center.x + 12);
    CHECK_EQ(poly.points[1].y, room_center.y + 12);
    CHECK_EQ(poly.points[2].x, room_center.x + 22);
    CHECK_EQ(poly.points[2].y, room_center.y - 8);
}

TEST_CASE("RoomAreaCache scales stored polygons when room resizes") {
    DevControls::RoomAreaCache cache;

    nlohmann::json room_json;
    room_json["areas"] = nlohmann::json::array({
        nlohmann::json{
            {"name", "spawn"},
            {"type", "spawn"},
            {"kind", "Spawn"},
            {"scale_to_room", true},
            {"origional_width", 100},
            {"origional_height", 50},
            {"anchor", nlohmann::json{{"x", 6}, {"y", -4}}},
            {"anchor_relative_to_center", true},
            {"points", nlohmann::json::array({
                nlohmann::json{{"x", -10}, {"y", -5}},
                nlohmann::json{{"x", 10}, {"y", -5}},
                nlohmann::json{{"x", 0}, {"y", 5}}
            })}
        }
    });

    const SDL_Point room_center{64, 128};
    const std::pair<int, int> room_dims{200, 150};

    const auto& polygons = cache.ensure_from_json(&room_json, room_center, room_dims);
    REQUIRE_EQ(polygons.size(), 1);
    const auto& poly = polygons.front();

    const int expected_anchor_x = room_center.x + 12; // 6 * (200 / 100)
    const int expected_anchor_y = room_center.y - 12; // -4 * (150 / 50)
    CHECK_EQ(poly.anchor.x, expected_anchor_x);
    CHECK_EQ(poly.anchor.y, expected_anchor_y);

    REQUIRE_EQ(poly.points.size(), 3);
    CHECK_EQ(poly.points[0].x, expected_anchor_x - 20);
    CHECK_EQ(poly.points[0].y, expected_anchor_y - 15);
    CHECK_EQ(poly.points[1].x, expected_anchor_x + 20);
    CHECK_EQ(poly.points[1].y, expected_anchor_y - 15);
    CHECK_EQ(poly.points[2].x, expected_anchor_x);
    CHECK_EQ(poly.points[2].y, expected_anchor_y + 15);
}
