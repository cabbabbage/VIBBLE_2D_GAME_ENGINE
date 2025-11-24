#include "check.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

#include <SDL.h>

#include "asset/asset_types.hpp"
#include "utils/grid.hpp"
#include "utils/range_util.hpp"

namespace {

constexpr int clamp_positive(int value) {
    return value < 0 ? 0 : value;
}

}

Check::Check(bool debug)
    : debug_(debug) {}

void Check::setDebug(bool debug) {
    debug_ = debug;
}

void Check::begin_session(vibble::grid::Grid& grid, int resolution) {
    grid_ = &grid;
    resolution_ = vibble::grid::clamp_resolution(resolution);
    cell_world_size_ = std::max(1, vibble::grid::delta(resolution_));
    clear_indices();
}

void Check::reset_session() {
    grid_ = nullptr;
    resolution_ = 0;
    cell_world_size_ = 1;
    clear_indices();
}

void Check::register_asset(Asset* asset, bool enforce_spacing, bool track_for_spacing) {
    if (!asset || !asset->info) {
        return;
    }
    if (!grid_) {
        if (enforce_spacing) {
            enforced_assets_.insert(asset);
        }
        if (track_for_spacing) {
            tracked_assets_.insert(asset);
        }
        return;
    }

    SDL_Point index = grid_->world_to_index(asset->pos, resolution_);
    const CellKey key = make_key(index);

    if (enforce_spacing) {
        enforced_assets_.insert(asset);
        enforced_cells_[key].push_back(asset);
        if (!asset->info->name.empty()) {
            enforced_name_cells_[asset->info->name][key].push_back(asset);
        }
    }

    if (track_for_spacing) {
        tracked_assets_.insert(asset);
        all_cells_[key].push_back(asset);
        if (!asset->info->name.empty()) {
            name_cells_[asset->info->name][key].push_back(asset);
        }
    }
}

bool Check::check(const std::shared_ptr<AssetInfo>& info,
                  const SDL_Point& test_pos,
                  const std::vector<Area>& exclusion_areas,
                  const std::vector<std::unique_ptr<Asset>>& assets,
                  bool respect_exclusion_zones,
                  bool enforce_spacing_for_candidate,
                  bool treat_as_edge_asset,
                  bool treat_as_map_asset,
                  int num_neighbors) const {
    (void)num_neighbors;

    if (!info) {
        if (debug_) std::cout << "[Check] AssetInfo is null\n";
        return false;
    }

    if (debug_) {
        std::cout << "[Check] Running checks at position (" << test_pos.x << ", " << test_pos.y
                  << ") for asset: " << info->name << "\n";
    }

    if (respect_exclusion_zones && is_in_exclusion_zone(test_pos, exclusion_areas)) {
        if (debug_) std::cout << "[Check] Point is inside exclusion zone.\n";
        return true;
    }

    if (!grid_) {
        return perform_spacing_checks_linear(info, test_pos, assets, enforce_spacing_for_candidate, treat_as_edge_asset, treat_as_map_asset);
    }

    return perform_spacing_checks_grid(info, test_pos, enforce_spacing_for_candidate, treat_as_edge_asset, treat_as_map_asset);
}

Check::CellKey Check::make_key(SDL_Point index) {
    return (static_cast<CellKey>(static_cast<std::uint32_t>(index.x)) << 32) |
           static_cast<std::uint32_t>(index.y);
}

void Check::clear_indices() {
    all_cells_.clear();
    enforced_cells_.clear();
    name_cells_.clear();
    enforced_name_cells_.clear();
    tracked_assets_.clear();
    enforced_assets_.clear();
}

void Check::gather_from_cells(const std::unordered_map<CellKey, std::vector<Asset*>>& cells,
                              SDL_Point pos,
                              int radius,
                              std::vector<Asset*>& out,
                              std::unordered_set<const Asset*>& seen) const {
    if (!grid_ || radius <= 0 || cells.empty()) {
        return;
    }

    SDL_Point origin_index = grid_->world_to_index(pos, resolution_);
    const int span = clamp_positive((radius + cell_world_size_ - 1) / cell_world_size_);

    for (int dy = -span; dy <= span; ++dy) {
        for (int dx = -span; dx <= span; ++dx) {
            SDL_Point idx{origin_index.x + dx, origin_index.y + dy};
            auto it = cells.find(make_key(idx));
            if (it == cells.end()) {
                continue;
            }
            for (Asset* asset : it->second) {
                if (!asset) {
                    continue;
                }
                if (seen.insert(asset).second) {
                    out.push_back(asset);
                }
            }
        }
    }
}

void Check::gather_from_named_cells(const std::unordered_map<std::string, std::unordered_map<CellKey, std::vector<Asset*>>>& cells,
                                    const std::string& name,
                                    SDL_Point pos,
                                    int radius,
                                    std::vector<Asset*>& out,
                                    std::unordered_set<const Asset*>& seen) const {
    if (!grid_ || radius <= 0 || name.empty()) {
        return;
    }

    auto it = cells.find(name);
    if (it == cells.end()) {
        return;
    }
    gather_from_cells(it->second, pos, radius, out, seen);
}

bool Check::perform_spacing_checks_grid(const std::shared_ptr<AssetInfo>& info,
                                        const SDL_Point& test_pos,
                                        bool enforce_spacing_for_candidate,
                                        bool treat_as_edge_asset,
                                        bool treat_as_map_asset) const {
    if (!info) {
        return false;
    }
    if (info->type == asset_types::boundary) {
        if (debug_) {
            std::cout << "[Check] boundary asset; skipping spacing checks.\n";
        }
        return false;
    }

    const bool track_candidate = !(treat_as_edge_asset || treat_as_map_asset);
    if (!track_candidate) {
        if (debug_) {
            std::cout << "[Check] Asset exempt from spacing checks (edge/map).\n";
        }
        return false;
    }

    const int min_all = info->min_distance_all;
    const int min_same = info->min_same_type_distance;

    if (min_all <= 0 && min_same <= 0) {
        return false;
    }

    if (min_all > 0) {
        std::vector<Asset*> neighbors;
        std::unordered_set<const Asset*> seen;
        gather_from_cells(enforced_cells_, test_pos, min_all, neighbors, seen);
        if (enforce_spacing_for_candidate) {
            gather_from_cells(all_cells_, test_pos, min_all, neighbors, seen);
        }
        for (Asset* asset : neighbors) {
            if (!asset || !asset->info) {
                continue;
            }
            if (Range::is_in_range(asset, test_pos, min_all)) {
                if (debug_) {
                    std::cout << "[Check] Min distance (all) violated by asset: "
                              << asset->info->name << " at (" << asset->pos.x << ", "
                              << asset->pos.y << ")\n";
                }
                return true;
            }
        }
    }

    if (min_same > 0 && !info->name.empty()) {
        std::vector<Asset*> neighbors;
        std::unordered_set<const Asset*> seen;
        gather_from_named_cells(enforced_name_cells_, info->name, test_pos, min_same, neighbors, seen);
        if (enforce_spacing_for_candidate) {
            gather_from_named_cells(name_cells_, info->name, test_pos, min_same, neighbors, seen);
        }
        for (Asset* asset : neighbors) {
            if (!asset || !asset->info) {
                continue;
            }
            if (asset->info->name != info->name) {
                continue;
            }
            if (Range::is_in_range(asset, test_pos, min_same)) {
                if (debug_) {
                    std::cout << "[Check] Min type distance violated by same-name asset: "
                              << asset->info->name << " at (" << asset->pos.x << ", "
                              << asset->pos.y << ")\n";
                }
                return true;
            }
        }
    }

    return false;
}

bool Check::perform_spacing_checks_linear(const std::shared_ptr<AssetInfo>& info,
                                          const SDL_Point& test_pos,
                                          const std::vector<std::unique_ptr<Asset>>& assets,
                                          bool enforce_spacing_for_candidate,
                                          bool treat_as_edge_asset,
                                          bool treat_as_map_asset) const {
    if (!info) {
        return false;
    }
    if (info->type == asset_types::boundary) {
        if (debug_) {
            std::cout << "[Check] boundary asset; skipping spacing checks.\n";
        }
        return false;
    }

    const bool track_candidate = !(treat_as_edge_asset || treat_as_map_asset);
    if (!track_candidate) {
        return false;
    }

    const int min_all = info->min_distance_all;
    const int min_same = info->min_same_type_distance;

    if (min_all <= 0 && min_same <= 0) {
        return false;
    }

    for (const auto& uptr : assets) {
        Asset* existing = uptr.get();
        if (!existing || !existing->info) {
            continue;
        }

        const bool existing_enforced = enforced_assets_.count(existing) > 0;
        const bool existing_tracked = tracked_assets_.count(existing) > 0;

        if (min_all > 0) {
            const bool should_check = existing_enforced || (enforce_spacing_for_candidate && existing_tracked);
            if (should_check && Range::is_in_range(existing, test_pos, min_all)) {
                if (debug_) {
                    std::cout << "[Check] Min distance (all) violated by asset: "
                              << existing->info->name << " at (" << existing->pos.x << ", "
                              << existing->pos.y << ")\n";
                }
                return true;
            }
        }

        if (min_same > 0 && !info->name.empty() && existing->info->name == info->name) {
            bool should_check = existing_enforced;
            if (enforce_spacing_for_candidate) {
                should_check = true;
            }
            if (should_check && Range::is_in_range(existing, test_pos, min_same)) {
                if (debug_) {
                    std::cout << "[Check] Min type distance violated by same-name asset: "
                              << existing->info->name << " at (" << existing->pos.x << ", "
                              << existing->pos.y << ")\n";
                }
                return true;
            }
        }
    }

    return false;
}

bool Check::is_in_exclusion_zone(const SDL_Point& pos, const std::vector<Area>& zones) const {
    for (const auto& area : zones) {
        if (area.contains_point(SDL_Point{pos.x, pos.y})) {
            if (debug_) std::cout << "[Check] Point (" << pos.x << ", " << pos.y << ") is inside an exclusion area.\n";
            return true;
        }
    }
    return false;
}
