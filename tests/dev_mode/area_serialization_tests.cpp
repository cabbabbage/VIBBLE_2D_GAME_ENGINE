#include "doctest/doctest.h"

#include "ENGINE/asset/asset_info.hpp"
#include "ENGINE/map_generation/room.hpp"

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

TEST_CASE("Asset area canonical codec rescales entries with scale changes") {
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

    const SDL_Point anchor_high = AssetInfo::AreaCodec::scaled_anchor(info);
    const std::vector<Area::Point> scaled_high{
        SDL_Point{anchor_high.x - 24, anchor_high.y - 30},
        SDL_Point{anchor_high.x + 12, anchor_high.y - 18},
        SDL_Point{anchor_high.x - 6, anchor_high.y - 48}
    };

    Area area("impassable", scaled_high);
    area.set_type("impassable");
    info.upsert_area_from_editor(area);
    auto* stored = info.find_area("impassable");
    REQUIRE(stored != nullptr);

    const nlohmann::json encoded = AssetInfo::AreaCodec::encode_entry(
        info, *stored, stored->get_type(), stored->get_type());

    info.set_scale_factor(0.75f);
    const float new_scale = safe_scale(info.scale_factor);
    const SDL_Point anchor_low = AssetInfo::AreaCodec::scaled_anchor(info);

    auto decoded = AssetInfo::AreaCodec::decode_entry(info, encoded);
    REQUIRE(decoded);
    REQUIRE(decoded->area);
    const auto& pts_low = decoded->area->get_points();
    REQUIRE_EQ(pts_low.size(), scaled_high.size());

    for (std::size_t i = 0; i < pts_low.size(); ++i) {
        const int canonical_x = encoded["points"][i]["x"].get<int>();
        const int canonical_y = encoded["points"][i]["y"].get<int>();
        const int expected_x = anchor_low.x + static_cast<int>(std::llround(canonical_x * new_scale));
        const int expected_y = anchor_low.y + static_cast<int>(std::llround(canonical_y * new_scale));
        CHECK_EQ(pts_low[i].x, expected_x);
        CHECK_EQ(pts_low[i].y, expected_y);
    }
}

TEST_CASE("Asset areas persist canonical bounds across scale factor changes") {
    const nlohmann::json payload = {
        {"asset_type", "object"},
        {"start", "default"},
        {"animations", nlohmann::json::object()},
        {"size_settings", { {"scale_percentage", 100.0f} }},
        {"areas", nlohmann::json::array()}
    };

    ScopedTestAsset scoped("area_regression", payload);
    AssetInfo info(scoped.asset_name());
    info.original_canvas_width = 64;
    info.original_canvas_height = 96;
    info.set_scale_factor(1.0f);

    const SDL_Point anchor_initial = AssetInfo::AreaCodec::scaled_anchor(info);
    const std::vector<Area::Point> polygon{
        SDL_Point{anchor_initial.x - 16, anchor_initial.y - 60},
        SDL_Point{anchor_initial.x + 16, anchor_initial.y - 60},
        SDL_Point{anchor_initial.x + 8, anchor_initial.y - 24},
        SDL_Point{anchor_initial.x - 12, anchor_initial.y - 12}
    };

    Area area("interaction", polygon);
    area.set_type("interaction");
    info.upsert_area_from_editor(area);
    auto* stored = info.find_area("interaction");
    REQUIRE(stored != nullptr);

    const nlohmann::json canonical_snapshot = AssetInfo::AreaCodec::encode_entry(
        info, *stored, stored->get_type(), stored->get_type());

    REQUIRE(info.update_info_json());

    info.set_scale_factor(0.5f);
    REQUIRE(info.update_info_json());

    AssetInfo reloaded(scoped.asset_name());
    reloaded.original_canvas_width = info.original_canvas_width;
    reloaded.original_canvas_height = info.original_canvas_height;
    reloaded.loadAnimations(nullptr);

    Area* reloaded_area = reloaded.find_area("interaction");
    REQUIRE(reloaded_area != nullptr);

    const nlohmann::json reloaded_canonical = AssetInfo::AreaCodec::encode_entry(
        reloaded, *reloaded_area, reloaded_area->get_type(), reloaded_area->get_type());

    CHECK_EQ(reloaded_canonical["points"], canonical_snapshot["points"]);

    const auto [min_x_initial, min_y_initial, max_x_initial, max_y_initial] = area.get_bounds();
    const auto [min_x_scaled, min_y_scaled, max_x_scaled, max_y_scaled] = reloaded_area->get_bounds();

    const float expected_scale = safe_scale(reloaded.scale_factor);
    const int expected_width = static_cast<int>(std::llround((max_x_initial - min_x_initial) * expected_scale));
    const int expected_height = static_cast<int>(std::llround((max_y_initial - min_y_initial) * expected_scale));

    CHECK_EQ(max_x_scaled - min_x_scaled, expected_width);
    CHECK_EQ(max_y_scaled - min_y_scaled, expected_height);
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
