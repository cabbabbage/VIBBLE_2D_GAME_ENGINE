#include "check.hpp"
#include <algorithm>
#include <limits>
#include <cmath>
#include <iostream>

Check::Check(bool debug)
    : debug_(debug)
{}

void Check::setDebug(bool debug) {
    debug_ = debug;
}

bool Check::check(const std::shared_ptr<AssetInfo>& info,
                  int test_x,
                  int test_y,
                  const std::vector<Area>& exclusion_areas,
                  const std::vector<std::unique_ptr<Asset>>& assets,
                  bool check_min_distance,
                  bool check_min_distance_all,
                  int num_neighbors) const
{
    if (!info) {
        if (debug_) std::cout << "[Check] AssetInfo is null\n";
        return false;
    }

    if (debug_) {
        std::cout << "[Check] Running checks at position ("
                  << test_x << ", " << test_y
                  << ") for asset: " << info->name << "\n";
    }

    if (is_in_exclusion_zone(test_x, test_y, exclusion_areas)) {
        if (debug_) std::cout << "[Check] Point is inside exclusion zone.\n";
        return true;
    }

    if (check_min_distance_all && info->min_distance_all > 0) {
        if (this->check_min_distance_all(info, { test_x, test_y }, assets)) {

            if (debug_) std::cout << "[Check] Minimum distance (all) violated.\n";
            return true;
        }
    }

    if (info->type == "boundary") {
        if (debug_) std::cout << "[Check] boundary asset; skipping spacing and type distance checks.\n";
        return false;
    }

    auto nearest = get_closest_assets(test_x, test_y, num_neighbors, assets);
    if (debug_) std::cout << "[Check] Found " << nearest.size() << " nearest assets.\n";

    // spacing-area overlap removed

    if (check_min_distance && info->min_same_type_distance > 0) {
        if (check_min_type_distance(info, { test_x, test_y }, assets)) {
            if (debug_) std::cout << "[Check] Minimum type distance violated.\n";
            return true;
        }
    }

    if (debug_) std::cout << "[Check] All checks passed.\n";
    return false;
}


bool Check::is_in_exclusion_zone(int x, int y, const std::vector<Area>& zones) const {
    for (const auto& area : zones) {
        if (area.contains_point({x, y})) {
            if (debug_) std::cout << "[Check] Point (" << x << ", " << y << ") is inside an exclusion area.\n";
            return true;
        }
    }
    return false;
}

std::vector<Asset*> Check::get_closest_assets(int x, int y, int max_count,
                                              const std::vector<std::unique_ptr<Asset>>& assets) const
{
    std::vector<std::pair<int, Asset*>> pairs;
    pairs.reserve(assets.size());

    for (const auto& uptr : assets) {
        Asset* a = uptr.get();
        if (!a || !a->info) continue;

        int dx = a->pos_X - x;
        int dy = a->pos_Y - y;
        int dist_sq = dx * dx + dy * dy;
        pairs.emplace_back(dist_sq, a);
    }

    if (pairs.size() > static_cast<size_t>(max_count)) {
        std::nth_element(pairs.begin(),
                         pairs.begin() + max_count,
                         pairs.end(),
                         [](auto& a, auto& b) { return a.first < b.first; });
        pairs.resize(max_count);
    }

    std::sort(pairs.begin(), pairs.end(),
              [](auto& a, auto& b) { return a.first < b.first; });

    std::vector<Asset*> closest;
    closest.reserve(pairs.size());
    for (auto& p : pairs) {
        closest.push_back(p.second);
        if (debug_) {
            std::cout << "[Check] Closest asset: " << p.second->info->name
                      << " at (" << p.second->pos_X << ", " << p.second->pos_Y
                      << "), dist_sq=" << p.first << "\n";
        }
    }
    return closest;
}

// spacing-area overlap function removed



bool Check::check_min_distance_all(const std::shared_ptr<AssetInfo>& info,
                                   const Point& pos,
                                   const std::vector<std::unique_ptr<Asset>>& assets) const
{
    if (!info || info->min_distance_all <= 0)
        return false;

    int min_dist_sq = info->min_distance_all * info->min_distance_all;
    for (const auto& uptr : assets) {
        Asset* existing = uptr.get();
        if (!existing || !existing->info) continue;

        int dx = existing->pos_X - pos.first;
        int dy = existing->pos_Y - pos.second;
        if (dx * dx + dy * dy < min_dist_sq) {
            if (debug_) {
                std::cout << "[Check] Min distance (all) violated by asset: "
                          << existing->info->name << " at ("
                          << existing->pos_X << ", " << existing->pos_Y << ")\n";
            }
            return true;
        }
    }
    return false;
}



bool Check::check_min_type_distance(const std::shared_ptr<AssetInfo>& info,
                                    const Point& pos,
                                    const std::vector<std::unique_ptr<Asset>>& assets) const
{
    if (!info || info->name.empty() || info->min_same_type_distance <= 0)
        return false;

    int min_dist_sq = info->min_same_type_distance * info->min_same_type_distance;
    for (const auto& uptr : assets) {
        Asset* existing = uptr.get();
        if (!existing || !existing->info) continue;

        if (existing->info->name != info->name)
            continue;

        int dx = existing->pos_X - pos.first;
        int dy = existing->pos_Y - pos.second;
        if (dx * dx + dy * dy < min_dist_sq) {
            if (debug_) {
                std::cout << "[Check] Min type distance violated by same-name asset: "
                          << existing->info->name << " at ("
                          << existing->pos_X << ", " << existing->pos_Y << ")\n";
            }
            return true;
        }
    }
    return false;
}
