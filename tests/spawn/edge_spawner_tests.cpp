#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <cmath>
#include <random>
#include <vector>

#include "spawn/methods/edge_spawner.hpp"
#include "spawn/spawn_info.hpp"
#include "utils/area.hpp"
#include "util/grid.hpp"

namespace {
Area make_square(const std::string& name, int half_extent) {
    std::vector<SDL_Point> pts = {
        { -half_extent, -half_extent },
        {  half_extent, -half_extent },
        {  half_extent,  half_extent },
        { -half_extent,  half_extent }
    };
    return Area(name, pts);
}

Area make_diamond(const std::string& name, int radius) {
    std::vector<SDL_Point> pts = {
        { 0, radius },
        { radius, 0 },
        { 0, -radius },
        { -radius, 0 }
    };
    return Area(name, pts);
}

SpawnInfo make_spawn_info(int quantity, int inset_percent) {
    SpawnInfo info;
    info.position = "Edge";
    info.quantity = quantity;
    info.edge_inset_percent = inset_percent;
    return info;
}

EdgeSpawner::PlacementContext make_context(std::mt19937& rng,
                                               vibble::grid::Grid& grid,
                                               int resolution,
                                               SDL_Point center,
                                               std::function<bool(SDL_Point)> overlaps = {}) {
    EdgeSpawner::PlacementContext ctx{rng, grid, resolution, center, std::move(overlaps)};
    return ctx;
}

} // namespace

TEST_CASE("EdgeSpawner plan positions align with perimeter") {
    EdgeSpawner spawner;
    Area area = make_square("square", 10);
    SpawnInfo info = make_spawn_info(4, 100);

    std::mt19937 rng(1337);
    vibble::grid::Grid& grid = vibble::grid::global_grid();
    auto placement = make_context(rng, grid, 0, area.get_center());

    auto positions = spawner.plan_positions(info, area, placement);
    REQUIRE_EQ(positions.size(), 4);

    const SDL_Point center = area.get_center();
    for (const SDL_Point& pt : positions) {
        const int dx = std::abs(pt.x - center.x);
        const int dy = std::abs(pt.y - center.y);
        CHECK((dx == 10 || dy == 10));
    }
}

TEST_CASE("EdgeSpawner inset collapses to center when zero") {
    EdgeSpawner spawner;
    Area area = make_square("square", 10);
    SpawnInfo info = make_spawn_info(3, 0);

    std::mt19937 rng(42);
    vibble::grid::Grid& grid = vibble::grid::global_grid();
    auto placement = make_context(rng, grid, 0, area.get_center());

    auto positions = spawner.plan_positions(info, area, placement);
    REQUIRE_EQ(positions.size(), 3);

    const SDL_Point center = area.get_center();
    for (const SDL_Point& pt : positions) {
        CHECK_EQ(pt.x, center.x);
        CHECK_EQ(pt.y, center.y);
    }
}

TEST_CASE("EdgeSpawner inset extends outward when doubled") {
    EdgeSpawner spawner;
    Area area = make_square("square", 10);

    SpawnInfo base = make_spawn_info(5, 100);
    SpawnInfo outward = make_spawn_info(5, 200);

    vibble::grid::Grid& grid = vibble::grid::global_grid();
    SDL_Point center = area.get_center();

    std::mt19937 rng_base(7);
    auto base_ctx = make_context(rng_base, grid, 0, center);
    auto base_positions = spawner.plan_positions(base, area, base_ctx);

    std::mt19937 rng_out(7);
    auto out_ctx = make_context(rng_out, grid, 0, center);
    auto out_positions = spawner.plan_positions(outward, area, out_ctx);

    REQUIRE_EQ(base_positions.size(), out_positions.size());
    for (std::size_t i = 0; i < base_positions.size(); ++i) {
        const SDL_Point& inner = base_positions[i];
        const SDL_Point& outer = out_positions[i];
        const double inner_dist = std::hypot(static_cast<double>(inner.x - center.x),
                                             static_cast<double>(inner.y - center.y));
        const double outer_dist = std::hypot(static_cast<double>(outer.x - center.x),
                                             static_cast<double>(outer.y - center.y));
        CHECK(outer_dist >= doctest::Approx(inner_dist));
    }
}

TEST_CASE("EdgeSpawner respects trail overlap filter") {
    EdgeSpawner spawner;
    Area area = make_square("square", 10);
    SpawnInfo info = make_spawn_info(3, 0);

    std::mt19937 rng(99);
    vibble::grid::Grid& grid = vibble::grid::global_grid();
    SDL_Point center = area.get_center();

    auto placement = make_context(rng, grid, 0, center, [center](SDL_Point pt) {
        return pt.x == center.x && pt.y == center.y;
    });

    auto positions = spawner.plan_positions(info, area, placement);
    CHECK(positions.empty());
}

TEST_CASE("EdgeSpawner snaps points to grid resolution") {
    EdgeSpawner spawner;
    Area area = make_diamond("diamond", 5);
    SpawnInfo info = make_spawn_info(6, 100);

    std::mt19937 rng(314);
    vibble::grid::Grid& grid = vibble::grid::global_grid();
    SDL_Point center = area.get_center();

    auto placement = make_context(rng, grid, 1, center);
    auto positions = spawner.plan_positions(info, area, placement);
    REQUIRE_FALSE(positions.empty());

    for (const SDL_Point& pt : positions) {
        CHECK(grid.is_vertex(pt, 1));
    }
}
