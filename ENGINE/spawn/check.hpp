#pragma once

#include <vector>
#include <memory>
#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"
#include "utils/area.hpp"

class Check {
public:
    explicit Check(bool debug);
    void setDebug(bool debug);
    bool check(const std::shared_ptr<AssetInfo>& info,
               int test_x,
               int test_y,
               const std::vector<Area>& exclusion_areas,
               const std::vector<std::unique_ptr<Asset>>& assets,
               bool check_spacing,
               bool check_min_distance,
               bool check_min_distance_all,
               int num_neighbors) const;
private:
    using Point = std::pair<int, int>;
    bool debug_;
    bool is_in_exclusion_zone(int x, int y, const std::vector<Area>& zones) const;
    std::vector<Asset*> get_closest_assets(int x, int y, int max_count,
                                           const std::vector<std::unique_ptr<Asset>>& assets) const;
    bool check_spacing_overlap(const std::shared_ptr<AssetInfo>& info,
                               int test_pos_X,
                               int test_pos_Y,
                               const std::vector<Asset*>& closest_assets) const;
    bool check_min_type_distance(const std::shared_ptr<AssetInfo>& info,
                                 const Point& pos,
                                 const std::vector<std::unique_ptr<Asset>>& assets) const;
    bool check_min_distance_all(const std::shared_ptr<AssetInfo>& info,
                                const Point& pos,
                                const std::vector<std::unique_ptr<Asset>>& assets) const;
};
