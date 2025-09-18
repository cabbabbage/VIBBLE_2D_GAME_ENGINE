#include "check.hpp"
#include <algorithm>
#include <limits>
#include <cmath>
#include <iostream>
#include <SDL.h>
#include "utils/range_util.hpp"
#include "asset/asset_types.hpp"

Check::Check(bool debug)
: debug_(debug)
{}

void Check::setDebug(bool debug) {
	debug_ = debug;
}

bool Check::check(const std::shared_ptr<AssetInfo>& info,
                  const SDL_Point& test_pos,
                  const std::vector<Area>& exclusion_areas,
                  const std::vector<std::unique_ptr<Asset>>& assets,
                  bool check_spacing,
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
		<< test_pos.x << ", " << test_pos.y
		<< ") for asset: " << info->name << "\n";
	}
	if (is_in_exclusion_zone(test_pos, exclusion_areas)) {
		if (debug_) std::cout << "[Check] Point is inside exclusion zone.\n";
		return true;
	}
	if (check_min_distance_all && info->min_distance_all > 0) {
		if (this->check_min_distance_all(info, test_pos, assets)) {
			if (debug_) std::cout << "[Check] Minimum distance (all) violated.\n";
			return true;
		}
	}
        if (info->type == asset_types::boundary) {
                if (debug_) std::cout << "[Check] boundary asset; skipping spacing and type distance checks.\n";
                return false;
        }
	auto nearest = get_closest_assets(test_pos, num_neighbors, assets);
	if (debug_) std::cout << "[Check] Found " << nearest.size() << " nearest assets.\n";
	if (check_spacing && info->find_area("spacing_area")) {
		if (check_spacing_overlap(info, test_pos, nearest)) {
			if (debug_) std::cout << "[Check] Spacing overlap detected.\n";
			return true;
		}
	}
	if (check_min_distance && info->min_same_type_distance > 0) {
		if (check_min_type_distance(info, test_pos, assets)) {
			if (debug_) std::cout << "[Check] Minimum type distance violated.\n";
			return true;
		}
	}
	if (debug_) std::cout << "[Check] All checks passed.\n";
	return false;
}

bool Check::is_in_exclusion_zone(const SDL_Point& pos, const std::vector<Area>& zones) const {
	for (const auto& area : zones) {
		if (area.contains_point(SDL_Point{ pos.x, pos.y })) {
			if (debug_) std::cout << "[Check] Point (" << pos.x << ", " << pos.y << ") is inside an exclusion area.\n";
			return true;
		}
	}
	return false;
}

std::vector<Asset*> Check::get_closest_assets(const SDL_Point& pos, int max_count,
                                              const std::vector<std::unique_ptr<Asset>>& assets) const
{
	std::vector<std::pair<double, Asset*>> pairs;
	pairs.reserve(assets.size());
	for (const auto& uptr : assets) {
		Asset* a = uptr.get();
		if (!a || !a->info) continue;
		const double d = Range::get_distance(SDL_Point{ pos.x, pos.y }, a);
		pairs.emplace_back(d * d, a);
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
			<< " at (" << p.second->pos.x << ", " << p.second->pos.y
			<< "), dist_sq=" << p.first << "\n";
		}
	}
	return closest;
}

bool Check::check_spacing_overlap(const std::shared_ptr<AssetInfo>& info,
                                  const SDL_Point& test_pos,
                                  const std::vector<Asset*>& closest_assets) const
{
	if (!info) return false;
	Area* spacing = info->find_area("spacing_area");
	if (!spacing) return false;
	Area test_area = *spacing;
	auto [tminx, tminy, tmaxx, tmaxy] = test_area.get_bounds();
	int th = tmaxy - tminy + 1;
        test_area.align(SDL_Point{test_pos.x, test_pos.y - th / 2});
	for (Asset* other : closest_assets) {
		if (!other || !other->info) continue;
                Area other_area("fallback", SDL_Point{other->pos.x, other->pos.y}, 1, 1, "Square", 0, std::numeric_limits<int>::max(), std::numeric_limits<int>::max());
		Area* o_spacing = other->info->find_area("spacing_area");
		if (o_spacing) {
			other_area = *o_spacing;
			auto [ominx, ominy, omaxx, omaxy] = other_area.get_bounds();
			int oh = omaxy - ominy + 1;
                        other_area.align(SDL_Point{other->pos.x, other->pos.y - oh / 2});
		}
		if (test_area.intersects(other_area)) {
			if (debug_) std::cout << "[Check] Overlap found between test area and asset: "
			<< other->info->name << "\n";
			return true;
		}
	}
	return false;
}

bool Check::check_min_distance_all(const std::shared_ptr<AssetInfo>& info,
                                   const SDL_Point& pos,
                                   const std::vector<std::unique_ptr<Asset>>& assets) const
{
	if (!info || info->min_distance_all <= 0)
	return false;
	for (const auto& uptr : assets) {
		Asset* existing = uptr.get();
		if (!existing || !existing->info) continue;
		if (Range::is_in_range(existing, pos, info->min_distance_all)) {
			if (debug_) {
					std::cout << "[Check] Min distance (all) violated by asset: "
					<< existing->info->name << " at ("
					<< existing->pos.x << ", " << existing->pos.y << ")\n";
			}
			return true;
		}
	}
	return false;
}

bool Check::check_min_type_distance(const std::shared_ptr<AssetInfo>& info,
                                    const SDL_Point& pos,
                                    const std::vector<std::unique_ptr<Asset>>& assets) const
{
	if (!info || info->name.empty() || info->min_same_type_distance <= 0)
	return false;
	for (const auto& uptr : assets) {
		Asset* existing = uptr.get();
		if (!existing || !existing->info) continue;
		if (existing->info->name != info->name)
		continue;
		if (Range::is_in_range(existing, pos, info->min_same_type_distance)) {
			if (debug_) {
					std::cout << "[Check] Min type distance violated by same-name asset: "
					<< existing->info->name << " at ("
					<< existing->pos.x << ", " << existing->pos.y << ")\n";
			}
			return true;
		}
	}
	return false;
}
