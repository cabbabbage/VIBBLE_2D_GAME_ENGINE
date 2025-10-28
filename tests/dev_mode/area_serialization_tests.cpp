#include "doctest/doctest.h"

#include "asset/asset_info.hpp"
#include "utils/area_helpers.hpp"
#include "map_generation/room.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>

namespace {

namespace fs = std::filesystem;

struct ScopedTestAsset {
    explicit ScopedTestAsset(const std::string& base_name, const nlohmann::json& payload) {
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

inline float safe_scale(float value) {
    return (value > 0.0f && std::isfinite(value)) ? value : 1.0f;
}

} // namespace

TEST_CASE("Asset area codec preserves render frame metadata and rescales points") {
    const nlohmann::json payload = {
        {"asset_type", "object"},
        {"start", "default"},
        {"animations", nlohmann::json::object()},
        {"size_settings", { {"scale_percentage", 125.0f} }},
        {"areas", nlohmann::json::array()}
    };

    ScopedTestAsset scoped("area_codec", payload);
    AssetInfo info(scoped.asset_name());
    info.original_canvas_width = 48;
    info.original_canvas_height = 60;
    info.set_scale_factor(1.25f);

    AssetInfo::NamedArea::RenderFrame frame;
    frame.width = 60;  // 48 * 1.25
    frame.height = 75; // 60 * 1.25
    frame.pivot_x = frame.width / 2;
    frame.pivot_y = frame.height;
    frame.pixel_scale = 1.25f;

    const std::vector<Area::Point> render_points{
        SDL_Point{frame.pivot_x - 24, frame.pivot_y - 30},
        SDL_Point{frame.pivot_x + 12, frame.pivot_y - 18},
        SDL_Point{frame.pivot_x - 6, frame.pivot_y - 48}
    };

    Area area("impassable", render_points, 2);
    area.set_type("impassable");
    info.upsert_area_from_editor(area, frame);
    auto* stored = info.find_area("impassable");
    REQUIRE(stored != nullptr);

    const nlohmann::json encoded = AssetInfo::AreaCodec::encode_entry(
        info, *stored, stored->get_type(), stored->get_type());

    REQUIRE_EQ(encoded["coordinate_space"].value("kind", ""), "render_space");
    CHECK_EQ(encoded["coordinate_space"].value("canvas_width", 0), frame.width);
    CHECK_EQ(encoded["coordinate_space"].value("canvas_height", 0), frame.height);
    REQUIRE(encoded["coordinate_space"].contains("pivot"));
    CHECK_EQ(encoded["coordinate_space"]["pivot"].value("x", 0), frame.pivot_x);
    CHECK_EQ(encoded["coordinate_space"]["pivot"].value("y", 0), frame.pivot_y);

    info.set_scale_factor(0.75f);
    const float new_scale = safe_scale(info.scale_factor);

    auto decoded = AssetInfo::AreaCodec::decode_entry(info, encoded);
    REQUIRE(decoded);
    REQUIRE(decoded->area);
    REQUIRE(decoded->render_frame);
    CHECK_EQ(decoded->render_frame->width, frame.width);
    CHECK_EQ(decoded->render_frame->height, frame.height);
    CHECK_EQ(decoded->render_frame->pivot_x, frame.pivot_x);
    CHECK_EQ(decoded->render_frame->pivot_y, frame.pivot_y);
    CHECK_EQ(doctest::Approx(decoded->render_frame->pixel_scale), frame.pixel_scale);

    const auto& pts_low = decoded->area->get_points();
    REQUIRE_EQ(pts_low.size(), render_points.size());

    const int base_width = info.original_canvas_width;
    const int base_height = info.original_canvas_height;
    const int scaled_width = static_cast<int>(std::llround(base_width * new_scale));
    const int scaled_height = static_cast<int>(std::llround(base_height * new_scale));
    const int pivot_x_new = static_cast<int>(std::llround(0.5 * scaled_width));
    const int pivot_y_new = static_cast<int>(std::llround(1.0 * scaled_height));

    for (std::size_t i = 0; i < pts_low.size(); ++i) {
        const int canonical_x = encoded["points"][i]["x"].get<int>();
        const int canonical_y = encoded["points"][i]["y"].get<int>();
        const int expected_x = pivot_x_new + static_cast<int>(std::llround(canonical_x * new_scale));
        const int expected_y = pivot_y_new + static_cast<int>(std::llround(canonical_y * new_scale));
        CHECK_EQ(pts_low[i].x, expected_x);
        CHECK_EQ(pts_low[i].y, expected_y);
    }
}

TEST_CASE("World areas honour per-area pivot metadata for flipped assets") {
    const nlohmann::json payload = {
        {"asset_type", "object"},
        {"start", "default"},
        {"animations", nlohmann::json::object()},
        {"size_settings", { {"scale_percentage", 100.0f} }},
        {"areas", nlohmann::json::array()}
    };

    ScopedTestAsset scoped("area_world", payload);
    AssetInfo info(scoped.asset_name());
    info.original_canvas_width = 0;
    info.original_canvas_height = 0;
    info.set_scale_factor(1.0f);

    AssetInfo::NamedArea::RenderFrame frame;
    frame.width = 96;
    frame.height = 128;
    frame.pivot_x = 48;
    frame.pivot_y = 128;
    frame.pixel_scale = 1.0f;

    const std::vector<Area::Point> local_points{
        SDL_Point{frame.pivot_x - 10, frame.pivot_y - 20},
        SDL_Point{frame.pivot_x + 20, frame.pivot_y - 40},
        SDL_Point{frame.pivot_x - 5, frame.pivot_y - 60}
    };

    Area area("interaction", local_points, 2);
    area.set_type("interaction");
    info.upsert_area_from_editor(area, frame);
    auto* stored = info.find_area("interaction");
    REQUIRE(stored != nullptr);

    SDL_Point world_pos{100, 200};
    Area world = area_helpers::make_world_area(info, *stored, world_pos, false);
    REQUIRE_EQ(world.get_points().size(), local_points.size());

    for (std::size_t i = 0; i < local_points.size(); ++i) {
        const int dx = local_points[i].x - frame.pivot_x;
        const int dy = local_points[i].y - frame.pivot_y;
        CHECK_EQ(world.get_points()[i].x, world_pos.x + dx);
        CHECK_EQ(world.get_points()[i].y, world_pos.y + dy);
    }

    Area flipped = area_helpers::make_world_area(info, *stored, world_pos, true);
    REQUIRE_EQ(flipped.get_points().size(), local_points.size());
    for (std::size_t i = 0; i < local_points.size(); ++i) {
        const int dx = local_points[i].x - frame.pivot_x;
        const int dy = local_points[i].y - frame.pivot_y;
        CHECK_EQ(flipped.get_points()[i].x, world_pos.x - dx);
        CHECK_EQ(flipped.get_points()[i].y, world_pos.y + dy);
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

TEST_CASE("Legacy room anchors default to room center when resolved") {
    const SDL_Point default_anchor{4000, 6000};
    nlohmann::json entry = {
        {"anchor", { {"x", 13397}, {"y", 10096} }},
        {"points", nlohmann::json::array({
            nlohmann::json::object({ {"x", -10}, {"y", -10} })
        })}
    };

    auto data = RoomAreaSerialization::resolve_anchor(
        entry, default_anchor, RoomAreaSerialization::Kind::Trigger);

    CHECK(data.relative_to_center);
    CHECK_EQ(data.world.x, default_anchor.x);
    CHECK_EQ(data.world.y, default_anchor.y);
    CHECK_EQ(data.relative_offset.x, 0);
    CHECK_EQ(data.relative_offset.y, 0);

    RoomAreaSerialization::write_anchor(entry, data, RoomAreaSerialization::Kind::Trigger);
    REQUIRE(entry.contains("anchor_relative_to_center"));
    CHECK(entry["anchor_relative_to_center"].get<bool>());
    CHECK_EQ(entry["anchor"].value("x", -1), 0);
    CHECK_EQ(entry["anchor"].value("y", -1), 0);
}

TEST_CASE("Relative room anchor offsets are preserved") {
    const SDL_Point default_anchor{100, 200};
    nlohmann::json entry = {
        {"anchor", { {"x", 12}, {"y", -8} }},
        {"anchor_relative_to_center", true},
        {"points", nlohmann::json::array({
            nlohmann::json::object({ {"x", 5}, {"y", 9} })
        })}
    };

    auto data = RoomAreaSerialization::resolve_anchor(
        entry, default_anchor, RoomAreaSerialization::Kind::Spawn);

    CHECK(data.relative_to_center);
    CHECK_EQ(data.world.x, default_anchor.x + 12);
    CHECK_EQ(data.world.y, default_anchor.y - 8);
    CHECK_EQ(data.relative_offset.x, 12);
    CHECK_EQ(data.relative_offset.y, -8);

    RoomAreaSerialization::write_anchor(entry, data, RoomAreaSerialization::Kind::Spawn);
    CHECK_EQ(entry["anchor"].value("x", 0), 12);
    CHECK_EQ(entry["anchor"].value("y", 0), -8);
    CHECK(entry["anchor_relative_to_center"].get<bool>());
}

TEST_CASE("Room named areas scale with room dimensions when flagged") {
    nlohmann::json room_json;
    room_json["min_width"] = 200;
    room_json["max_width"] = 200;
    room_json["min_height"] = 150;
    room_json["max_height"] = 150;
    room_json["areas"] = nlohmann::json::array({
        nlohmann::json{
            {"name", "spawn_field"},
            {"type", "spawn"},
            {"kind", "Spawn"},
            {"scale_to_room", true},
            {"origional_width", 100},
            {"origional_height", 50},
            {"anchor", nlohmann::json{{"x", 0}, {"y", 0}}},
            {"anchor_relative_to_center", true},
            {"resolution", 2},
            {"points", nlohmann::json::array({
                nlohmann::json{{"x", -10}, {"y", -5}},
                nlohmann::json{{"x", 10}, {"y", -5}},
                nlohmann::json{{"x", 0}, {"y", 5}}
            })}
        }
    });

    MapGridSettings grid_settings{};
    Room room_a(Room::Point{0, 0},
                "test",
                "RoomA",
                nullptr,
                std::string{},
                nullptr,
                nullptr,
                &room_json,
                nullptr,
                grid_settings,
                0.0,
                "rooms");

    REQUIRE_EQ(room_a.areas.size(), 1);
    const auto& named_a = room_a.areas.front();
    CHECK(named_a.scale_to_room);
    CHECK_EQ(named_a.original_room_width, 200);
    CHECK_EQ(named_a.original_room_height, 150);
    const auto& pts_a = named_a.area->get_points();
    REQUIRE_EQ(pts_a.size(), 3);
    CHECK_EQ(pts_a[0].x, -20);
    CHECK_EQ(pts_a[0].y, -15);
    CHECK_EQ(pts_a[1].x, 20);
    CHECK_EQ(pts_a[1].y, -15);
    CHECK_EQ(pts_a[2].x, 0);
    CHECK_EQ(pts_a[2].y, 15);

    auto& stored_a = room_a.assets_data()["areas"][0];
    CHECK(stored_a.value("scale_to_room", false));
    CHECK_EQ(stored_a.value("origional_width", 0), 200);
    CHECK_EQ(stored_a.value("origional_height", 0), 150);
    CHECK_EQ(stored_a["points"][0].value("x", 0), -20);
    CHECK_EQ(stored_a["points"][0].value("y", 0), -15);

    room_json["min_width"] = 400;
    room_json["max_width"] = 400;
    room_json["min_height"] = 300;
    room_json["max_height"] = 300;

    Room room_b(Room::Point{0, 0},
                "test",
                "RoomB",
                nullptr,
                std::string{},
                nullptr,
                nullptr,
                &room_json,
                nullptr,
                grid_settings,
                0.0,
                "rooms");

    REQUIRE_EQ(room_b.areas.size(), 1);
    const auto& named_b = room_b.areas.front();
    CHECK(named_b.scale_to_room);
    CHECK_EQ(named_b.original_room_width, 400);
    CHECK_EQ(named_b.original_room_height, 300);
    const auto& pts_b = named_b.area->get_points();
    REQUIRE_EQ(pts_b.size(), 3);
    CHECK_EQ(pts_b[0].x, -40);
    CHECK_EQ(pts_b[0].y, -30);
    CHECK_EQ(pts_b[1].x, 40);
    CHECK_EQ(pts_b[1].y, -30);
    CHECK_EQ(pts_b[2].x, 0);
    CHECK_EQ(pts_b[2].y, 30);

    Area resave(named_b.name, named_b.area->get_points(), named_b.area->resolution());
    resave.set_type(named_b.area->get_type());
    room_b.upsert_named_area(resave, named_b.type, true, 0, 0);

    auto& stored_b = room_b.assets_data()["areas"][0];
    CHECK(stored_b.value("scale_to_room", false));
    CHECK_EQ(stored_b.value("origional_width", 0), 400);
    CHECK_EQ(stored_b.value("origional_height", 0), 300);
}
