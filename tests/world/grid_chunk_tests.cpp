#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "world/grid.hpp"

#include <set>

namespace {
constexpr int kResolution = 3; // 1 << 3 == 8px chunks
const SDL_Point kOrigin{0, 0};
}

TEST_CASE("ChunkManager::from_world floors toward negative infinity") {
    world::ChunkManager manager;
    world::Chunk& neg_x_chunk = manager.ensure(-1, 0, kResolution, kOrigin);
    world::Chunk& pos_chunk = manager.ensure(0, 0, kResolution, kOrigin);
    world::Chunk& neg_xy_chunk = manager.ensure(-1, -1, kResolution, kOrigin);
    world::Chunk& far_neg_chunk = manager.ensure(-2, 0, kResolution, kOrigin);

    CHECK(manager.from_world(SDL_Point{-1, 0}, kResolution, kOrigin) == &neg_x_chunk);
    CHECK(manager.from_world(SDL_Point{-8, 0}, kResolution, kOrigin) == &neg_x_chunk);
    CHECK(manager.from_world(SDL_Point{-9, 0}, kResolution, kOrigin) == &far_neg_chunk);

    CHECK(manager.from_world(SDL_Point{0, 0}, kResolution, kOrigin) == &pos_chunk);
    CHECK(manager.from_world(SDL_Point{-1, -1}, kResolution, kOrigin) == &neg_xy_chunk);
}

TEST_CASE("Grid::chunk_from_world matches ensure indices across origin") {
    world::Grid grid(kOrigin, kResolution);
    world::Chunk& neg_x_chunk = grid.get_or_create_chunk_ij(-1, 0);
    world::Chunk& pos_chunk = grid.get_or_create_chunk_ij(0, 0);
    world::Chunk& neg_y_chunk = grid.get_or_create_chunk_ij(0, -1);
    world::Chunk& neg_xy_chunk = grid.get_or_create_chunk_ij(-1, -1);

    CHECK(grid.chunk_from_world(SDL_Point{-1, 0}) == &neg_x_chunk);
    CHECK(grid.chunk_from_world(SDL_Point{-8, 0}) == &neg_x_chunk);
    CHECK(grid.chunk_from_world(SDL_Point{0, 0}) == &pos_chunk);
    CHECK(grid.chunk_from_world(SDL_Point{0, -1}) == &neg_y_chunk);
    CHECK(grid.chunk_from_world(SDL_Point{-1, -1}) == &neg_xy_chunk);
}

TEST_CASE("Grid::ensure_chunk_from_world creates chunks matching world coordinates") {
    world::Grid grid(kOrigin, kResolution);

    SDL_Point world_a{-8, 0};
    SDL_Point world_b{16, 8};

    world::Chunk* chunk_a = grid.ensure_chunk_from_world(world_a);
    REQUIRE(chunk_a != nullptr);
    CHECK(chunk_a == grid.chunk_from_world(world_a));

    world::Chunk* chunk_b = grid.ensure_chunk_from_world(world_b);
    REQUIRE(chunk_b != nullptr);
    CHECK(chunk_b == grid.chunk_from_world(world_b));
    CHECK(chunk_b->i == 2);
    CHECK(chunk_b->j == 1);
}

TEST_CASE("Grid::update_active_chunks includes chunks on both sides of origin") {
    world::Grid grid(kOrigin, kResolution);

    const SDL_Rect camera{-4, -4, 8, 8};
    grid.update_active_chunks(camera, 0);

    const auto& active = grid.active_chunks();
    std::set<std::pair<int, int>> coords;
    for (const world::Chunk* chunk : active) {
        REQUIRE(chunk != nullptr);
        coords.emplace(chunk->i, chunk->j);
    }

    CHECK(coords.count({-1, -1}) == 1);
    CHECK(coords.count({-1, 0}) == 1);
    CHECK(coords.count({0, -1}) == 1);
    CHECK(coords.count({0, 0}) == 1);
    CHECK(coords.size() == 4);
}
