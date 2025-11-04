#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <SDL.h>

#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"
#include "utils/area.hpp"

namespace vibble::grid {
class Grid;
}

class Check {
public:
    explicit Check(bool debug);
    void setDebug(bool debug);

    void begin_session(vibble::grid::Grid& grid, int resolution);
    void reset_session();

    void register_asset(Asset* asset, bool enforce_spacing, bool track_for_spacing);

    bool check(const std::shared_ptr<AssetInfo>& info, const SDL_Point& test_pos, const std::vector<Area>& exclusion_areas, const std::vector<std::unique_ptr<Asset>>& assets, bool respect_exclusion_zones, bool enforce_spacing_for_candidate, bool treat_as_edge_asset, bool treat_as_map_asset, int num_neighbors) const;

private:
    bool debug_;

    vibble::grid::Grid* grid_ = nullptr;
    int resolution_ = 0;
    int cell_world_size_ = 1;

    using CellKey = std::uint64_t;

    static CellKey make_key(SDL_Point index);

    void clear_indices();

    void gather_from_cells(const std::unordered_map<CellKey, std::vector<Asset*>>& cells, SDL_Point pos, int radius, std::vector<Asset*>& out, std::unordered_set<const Asset*>& seen) const;

    void gather_from_named_cells(const std::unordered_map<std::string, std::unordered_map<CellKey, std::vector<Asset*>>>& cells, const std::string& name, SDL_Point pos, int radius, std::vector<Asset*>& out, std::unordered_set<const Asset*>& seen) const;

    bool is_in_exclusion_zone(const SDL_Point& pos, const std::vector<Area>& zones) const;

    bool perform_spacing_checks_grid(const std::shared_ptr<AssetInfo>& info, const SDL_Point& test_pos, bool enforce_spacing_for_candidate, bool treat_as_edge_asset, bool treat_as_map_asset) const;

    bool perform_spacing_checks_linear(const std::shared_ptr<AssetInfo>& info, const SDL_Point& test_pos, const std::vector<std::unique_ptr<Asset>>& assets, bool enforce_spacing_for_candidate, bool treat_as_edge_asset, bool treat_as_map_asset) const;

    std::unordered_map<CellKey, std::vector<Asset*>> all_cells_;
    std::unordered_map<CellKey, std::vector<Asset*>> enforced_cells_;
    std::unordered_map<std::string, std::unordered_map<CellKey, std::vector<Asset*>>> name_cells_;
    std::unordered_map<std::string, std::unordered_map<CellKey, std::vector<Asset*>>> enforced_name_cells_;
    std::unordered_set<const Asset*> tracked_assets_;
    std::unordered_set<const Asset*> enforced_assets_;
};
