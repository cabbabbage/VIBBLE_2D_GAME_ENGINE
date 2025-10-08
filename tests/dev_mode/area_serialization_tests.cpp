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

    Area area("impassable", render_points);
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

    Area area("interaction", local_points);
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
