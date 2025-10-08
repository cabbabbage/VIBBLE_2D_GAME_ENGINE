#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <random>
#include <vector>

#include "utils/map_grid.hpp"

TEST_CASE("get_all_points_in_area respects occupancy at grid edges") {
    MapGrid grid(20, 20, 10, SDL_Point{0, 0});
    Area edge_area("edge", std::vector<SDL_Point>{
        SDL_Point{-5, -5},
        SDL_Point{5, -5},
        SDL_Point{5, 25},
        SDL_Point{-5, 25}
    });

    MapGrid::Point* mid = grid.point_at(SDL_Point{0, 10});
    REQUIRE(mid != nullptr);
    grid.set_occupied(mid, true);

    auto points = grid.get_all_points_in_area(edge_area);
    REQUIRE_EQ(points.size(), 2);
    CHECK_EQ(points[0]->pos.x, 0);
    CHECK_EQ(points[0]->pos.y, 0);
    CHECK_EQ(points[1]->pos.x, 0);
    CHECK_EQ(points[1]->pos.y, 20);
}

TEST_CASE("get_rnd_point_in_area returns free point within bounded sub-range") {
    MapGrid grid(20, 20, 10, SDL_Point{0, 0});
    Area corner_area("corner", std::vector<SDL_Point>{
        SDL_Point{15, 15},
        SDL_Point{25, 15},
        SDL_Point{25, 25},
        SDL_Point{15, 25}
    });

    std::mt19937 rng(1337);
    MapGrid::Point* pick = grid.get_rnd_point_in_area(corner_area, rng);
    REQUIRE(pick != nullptr);
    CHECK_EQ(pick->pos.x, 20);
    CHECK_EQ(pick->pos.y, 20);

    grid.set_occupied(pick, true);
    CHECK(grid.get_rnd_point_in_area(corner_area, rng) == nullptr);
}
