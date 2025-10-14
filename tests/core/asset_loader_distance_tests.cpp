#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "core/asset_loader_internal.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace {

double naive_min_distance_sq(const SDL_Point& point, const std::vector<const Area*>& zones) {
    double best = std::numeric_limits<double>::infinity();
    for (const Area* zone : zones) {
        if (!zone) continue;
        const auto& pts = zone->get_points();
        if (pts.size() < 2) continue;
        for (size_t i = 0; i + 1 < pts.size(); ++i) {
            const SDL_Point& p1 = pts[i];
            const SDL_Point& p2 = pts[(i + 1) % pts.size()];
            const double vx = static_cast<double>(p2.x - p1.x);
            const double vy = static_cast<double>(p2.y - p1.y);
            const double wx = static_cast<double>(point.x - p1.x);
            const double wy = static_cast<double>(point.y - p1.y);
            const double len2 = vx * vx + vy * vy;
            double t = len2 > 0.0 ? (vx * wx + vy * wy) / len2 : 0.0;
            t = std::clamp(t, 0.0, 1.0);
            const double projx = static_cast<double>(p1.x) + t * vx;
            const double projy = static_cast<double>(p1.y) + t * vy;
            const double dx = projx - static_cast<double>(point.x);
            const double dy = projy - static_cast<double>(point.y);
            const double distSq = dx * dx + dy * dy;
            if (distSq < best) {
                best = distSq;
            }
        }
    }
    return best;
}

std::vector<const Area*> make_zones() {
    static Area near_room("near", std::vector<Area::Point>{
        SDL_Point{0, 0}, SDL_Point{0, 200}, SDL_Point{200, 200}, SDL_Point{200, 0}
    });
    static Area far_room("far", std::vector<Area::Point>{
        SDL_Point{2000, 2000}, SDL_Point{2000, 2200}, SDL_Point{2200, 2200}, SDL_Point{2200, 2000}
    });
    static Area diagonal_room("diag", std::vector<Area::Point>{
        SDL_Point{-1500, 500}, SDL_Point{-1300, 700}, SDL_Point{-1100, 500}, SDL_Point{-1300, 300}
    });
    return {&near_room, &far_room, &diagonal_room};
}

} // namespace

TEST_CASE("Zone cache preserves inside checks for multiple rooms") {
    auto zones = make_zones();
    auto cache = asset_loader_internal::build_zone_cache(zones);

    SDL_Point inside_near{50, 50};
    SDL_Point inside_far{2100, 2100};
    SDL_Point outside{800, 800};

    CHECK(asset_loader_internal::point_inside_any_zone(inside_near, cache));
    CHECK(asset_loader_internal::point_inside_any_zone(inside_far, cache));
    CHECK_FALSE(asset_loader_internal::point_inside_any_zone(outside, cache));
}

TEST_CASE("Zone cache distance matches naive computation with distant zones") {
    auto zones = make_zones();
    auto cache = asset_loader_internal::build_zone_cache(zones);

    std::vector<SDL_Point> samples{
        SDL_Point{500, -400},
        SDL_Point{1500, 1500},
        SDL_Point{-1600, 450},
        SDL_Point{4000, 4000}
    };

    const int remove_threshold = 1200;
    for (const SDL_Point& p : samples) {
        const double cached = asset_loader_internal::min_distance_sq_to_zones(p, cache, remove_threshold);
        const double naive = naive_min_distance_sq(p, zones);
        if (std::isfinite(naive)) {
            CHECK(cached == doctest::Approx(naive).epsilon(1e-9));
        } else {
            CHECK_FALSE(std::isfinite(cached));
        }
    }
}
