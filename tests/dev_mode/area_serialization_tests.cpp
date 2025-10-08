#include "doctest/doctest.h"

#include "ENGINE/asset_info_methods/area_loader.hpp"
#include "ENGINE/map_generation/room.hpp"

TEST_CASE("Asset area serialization round-trip preserves points") {
    const std::vector<Area::Point> world{
        Area::Point{10, 20},
        Area::Point{18, 35},
        Area::Point{6, 28}
    };
    SDL_Point anchor{4, 12};

    nlohmann::json entry;
    entry["points"] = AreaLoader::encode_points(world, anchor);

    auto decoded = AreaLoader::decode_points(entry, anchor);
    REQUIRE_EQ(decoded.size(), world.size());
    for (std::size_t i = 0; i < world.size(); ++i) {
        CHECK_EQ(decoded[i].x, world[i].x);
        CHECK_EQ(decoded[i].y, world[i].y);
    }
}

TEST_CASE("Room area serialization round-trip and kind validation") {
    const std::vector<SDL_Point> world{
        SDL_Point{100, 200},
        SDL_Point{140, 190},
        SDL_Point{120, 230}
    };
    SDL_Point anchor{90, 180};

    nlohmann::json entry;
    entry["points"] = RoomAreaSerialization::encode_points(world, anchor);

    auto decoded = RoomAreaSerialization::decode_points(entry, anchor);
    REQUIRE_EQ(decoded.size(), world.size());
    for (std::size_t i = 0; i < world.size(); ++i) {
        CHECK_EQ(decoded[i].x, world[i].x);
        CHECK_EQ(decoded[i].y, world[i].y);
    }

    nlohmann::json good_meta = {
        {"kind", "Trigger"},
        {"points", entry["points"]}
    };
    auto good_kind = RoomAreaSerialization::infer_kind_from_entry(good_meta, "trigger", "area");
    CHECK(RoomAreaSerialization::is_supported_kind(good_kind));

    nlohmann::json bad_meta = {
        {"kind", "Other"},
        {"points", entry["points"]}
    };
    auto bad_kind = RoomAreaSerialization::infer_kind_from_entry(bad_meta, "other", "misc");
    CHECK_FALSE(RoomAreaSerialization::is_supported_kind(bad_kind));
}
