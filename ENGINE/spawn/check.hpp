#pragma once

#include <vector>
#include <memory>
#include <SDL.h>
#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"
#include "utils/area.hpp"

class Check {
public:
    explicit Check(bool debug);
    void setDebug(bool debug);

    bool check(const std::shared_ptr<AssetInfo>& info,
               const SDL_Point& test_pos,
               const std::vector<Area>& exclusion_areas,
               const std::vector<std::unique_ptr<Asset>>& assets,
               bool check_spacing,
               bool check_min_distance,
               bool check_min_distance_all,
               int num_neighbors) const;

private:
    bool debug_;

    bool is_in_exclusion_zone(const SDL_Point& pos,
                              const std::vector<Area>& zones) const;

    std::vector<Asset*> get_closest_assets(const SDL_Point& pos,
                                           int max_count,
                                           const std::vector<std::unique_ptr<Asset>>& assets) const;

    bool check_spacing_overlap(const std::shared_ptr<AssetInfo>& info,
                               const SDL_Point& test_pos,
                               const std::vector<Asset*>& closest_assets) const;

    bool check_min_type_distance(const std::shared_ptr<AssetInfo>& info,
                                 const SDL_Point& pos,
                                 const std::vector<std::unique_ptr<Asset>>& assets) const;

    bool check_min_distance_all(const std::shared_ptr<AssetInfo>& info,
                                const SDL_Point& pos,
                                const std::vector<std::unique_ptr<Asset>>& assets) const;
};
