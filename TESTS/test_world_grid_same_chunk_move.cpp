#include "doctest/doctest.h"

#include "world/world_grid.hpp"
#include "asset/Asset.hpp" // Resolved to TESTS/stubs/asset/Asset.hpp in the isolated test target

using namespace world;

TEST_CASE("WorldGrid same-chunk move rebinds GridPoint without duplication") {
    // Chunk resolution r=4 -> step=16px; moving <16px stays in same chunk and grid point.
    WorldGrid grid(SDL_Point{0, 0}, /*r_chunk=*/4);

    // Minimal asset setup (stubbed Asset)
    SDL_Point start{10, 10};
    auto* asset = new Asset();
    asset->pos = start;

    // Register asset in the grid (transfers ownership)
    grid.register_asset(asset);

    GridPoint* gp0 = grid.point_for_asset(asset);
    REQUIRE(gp0 != nullptr);
    const GridId id0 = gp0->id;
    REQUIRE(gp0->occupants.size() == 1);
    CHECK(gp0->world.x == start.x);
    CHECK(gp0->world.y == start.y);

    // Move within the same chunk/grid cell
    SDL_Point next{12, 12}; // still within 16px cell at r=4
    asset->pos = next; // keep stub's pos in sync with the new world position
    grid.move_asset(asset, start, next);

    GridPoint* gp1 = grid.point_for_asset(asset);
    REQUIRE(gp1 != nullptr);
    // Grid point id should be stable when staying in same cell
    CHECK(gp1->id == id0);
    // World coordinates should update to the new position
    CHECK(gp1->world.x == next.x);
    CHECK(gp1->world.y == next.y);
    // No duplicate occupants should be created
    CHECK(gp1->occupants.size() == 1);
    // Asset should keep the same grid id
    CHECK(asset->grid_id() == id0);
}
