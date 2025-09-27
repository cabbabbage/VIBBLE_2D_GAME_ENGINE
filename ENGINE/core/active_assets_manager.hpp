#pragma once

#include "asset/Asset.hpp"
#include "render/camera.hpp"

#include <vector>
#include <cstddef>
#include <memory>

class ActiveAssetsManager {

	public:
    ActiveAssetsManager(int screen_width, int screen_height, camera& v);
    void initialize(std::vector<Asset*>& all_assets, Asset* player, int screen_center_x, int screen_center_y);
    void updateAssetVectors(Asset* player, int screen_center_x, int screen_center_y);
    void updateClosestAssets(Asset* player, std::size_t max_count);
    void sortByZIndex();
    void activate(Asset* asset);
    void remove(Asset* asset);
    void markNeedsSort();
    int update_activate_interval = 15;
    int update_closest_interval  = 2;
    const std::vector<Asset*>& getActive() const;
    const std::vector<Asset*>& getClosest() const { return closest_assets_; }
    const std::vector<Asset*>& getImpassableClosest() const { return impassable_assets_; }
    const std::vector<Asset*>& getInteractiveClosest() const { return interactive_assets_; }

	private:
    void updateActiveAssets(int cx, int cy);
    void addActiveUnsorted(Asset* asset);
    void rebuild_combined_if_needed() const;
    static bool is_texture(const Asset* a);
    int activate_counter_ = 0;
    int closest_counter_  = 0;
    camera& camera_;
    int screen_width_;
    int screen_height_;
    std::vector<Asset*>* all_assets_;
    // We maintain two buckets to avoid sorting textures.
    std::vector<Asset*> textures_;
    std::vector<Asset*> others_;
    // Combined view returned by getActive(). Mark dirty when buckets change.
    mutable std::vector<Asset*> active_assets_;
    std::vector<Asset*> closest_assets_;
    std::vector<Asset*> impassable_assets_;
    std::vector<Asset*> interactive_assets_;
    bool needs_sort_ = false;
    mutable bool combined_dirty_ = true;
};
