#pragma once

#include "asset/Asset.hpp"
#include "utils/view.hpp"

#include <vector>
#include <unordered_set>
#include <cstddef>
#include <memory>

class ActiveAssetsManager {
public:
    ActiveAssetsManager(int screen_width, int screen_height, view& v);
    void initialize(std::vector<Asset*>& all_assets,
                    Asset* player,
                    int screen_center_x,
                    int screen_center_y);
    void updateAssetVectors(Asset* player,
                            int screen_center_x,
                            int screen_center_y);
    void updateClosestAssets(Asset* player, std::size_t max_count);
    void sortByZIndex();
    void activate(Asset* asset);
    void remove(Asset* asset);
    int update_activate_interval = 15;
    int update_closest_interval  = 2;
    const std::vector<Asset*>& getActive() const { return active_assets_; }
    const std::vector<Asset*>& getClosest() const { return closest_assets_; }
    const std::vector<Asset*>& getImpassableClosest() const { return impassable_assets_; }
    const std::vector<Asset*>& getInteractiveClosest() const { return interactive_assets_; }
private:
    void updateActiveAssets(int cx, int cy);
    int activate_counter_ = 0;
    int closest_counter_  = 0;
    view& view_;
    int screen_width_;
    int screen_height_;
    std::vector<Asset*>* all_assets_;
    std::vector<Asset*> active_assets_;
    std::vector<Asset*> closest_assets_;
    std::vector<Asset*> impassable_assets_;
    std::vector<Asset*> interactive_assets_;
};
