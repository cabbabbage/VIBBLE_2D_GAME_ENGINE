#pragma once

#include <SDL.h>

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "asset/Asset.hpp"
#include "util/range.hpp"

class AssetList {
   public:
    enum class SortMode {
        Unsorted,
        ZIndexAsc,
        ZIndexDesc,
    };

    AssetList(const std::vector<Asset*>& source_candidates,
              SDL_Point list_center,
              int search_radius,
              const std::vector<std::string>& required_tags,
              const std::vector<std::string>& top_bucket_tags,
              const std::vector<std::string>& bottom_bucket_tags,
              SortMode sort_mode);

    AssetList(const std::vector<Asset*>& source_candidates,
              Asset* center_asset,
              int search_radius,
              const std::vector<std::string>& required_tags,
              const std::vector<std::string>& top_bucket_tags,
              const std::vector<std::string>& bottom_bucket_tags,
              SortMode sort_mode);

    AssetList(const AssetList& parent_list,
              SDL_Point list_center,
              int search_radius,
              const std::vector<std::string>& required_tags,
              const std::vector<std::string>& top_bucket_tags,
              const std::vector<std::string>& bottom_bucket_tags,
              SortMode sort_mode);

    AssetList(const AssetList& parent_list,
              Asset* center_asset,
              int search_radius,
              const std::vector<std::string>& required_tags,
              const std::vector<std::string>& top_bucket_tags,
              const std::vector<std::string>& bottom_bucket_tags,
              SortMode sort_mode);

    void add_child(std::unique_ptr<AssetList> child);
    const std::vector<std::unique_ptr<AssetList>>& children() const { return children_; }

    const std::vector<Asset*>& top_unsorted() const { return list_top_unsorted_; }
    const std::vector<Asset*>& middle_sorted() const { return list_middle_sorted_; }
    const std::vector<Asset*>& bottom_unsorted() const { return list_bottom_unsorted_; }
    void full_list(std::vector<Asset*>& out) const;

    void set_center(SDL_Point p);
    void set_center(Asset* a);
    void set_search_radius(int r) { search_radius_ = r; }
    void set_sort_mode(SortMode m);
    void set_tags(const std::vector<std::string>& required_tags,
                  const std::vector<std::string>& top_bucket_tags,
                  const std::vector<std::string>& bottom_bucket_tags);

    void update();
    void update(SDL_Point new_center);

    std::vector<Asset*> get_union(const AssetList& other,
                                  const std::vector<std::string>& required_tags) const;

   private:
    SDL_Point resolve_center() const;
    void rebuild_from_scratch();
    void route_asset_to_section(Asset* a);
    void remove_from_all_sections(Asset* a);
    bool has_all_required_tags(const Asset* a, const std::vector<std::string>& req) const;
    bool has_any_tag(const Asset* a, const std::vector<std::string>& tags) const;

    void get_delta_area_assets(SDL_Point prev_center,
                               int prev_radius,
                               SDL_Point curr_center,
                               int curr_radius,
                               const std::vector<Asset*>& candidates,
                               std::vector<Asset*>& out_changed) const;

    void rebuild_candidate_lookup();
    void sync_with_parent_candidates();
    void prune_to_candidates();
    void apply_sort_mode();
    void set_parent(AssetList* parent) { parent_ = parent; }


   private:
    std::vector<Asset*> source_candidates_{};
    SDL_Point           center_point_{};
    Asset*              center_asset_ = nullptr;
    int                 search_radius_ = 0;
    std::vector<std::string> required_tags_{};
    std::vector<std::string> top_bucket_tags_{};
    std::vector<std::string> bottom_bucket_tags_{};
    SortMode                sort_mode_ = SortMode::Unsorted;

    std::vector<Asset*> list_top_unsorted_{};
    std::vector<Asset*> list_middle_sorted_{};
    std::vector<Asset*> list_bottom_unsorted_{};

    std::vector<Asset*> list_always_ineligible_{};

    std::vector<std::unique_ptr<AssetList>> children_{};

    SDL_Point previous_center_point_{};
    int       previous_search_radius_ = 0;

    std::vector<Asset*> scratch_delta_{};
    std::vector<Asset*> scratch_parent_full_{};
    std::vector<Asset*> pending_force_recheck_{};


    std::unordered_set<Asset*> always_ineligible_lookup_{};
    std::unordered_set<Asset*> candidates_lookup_{};
    std::unordered_set<Asset*> scratch_membership_set_{};

    AssetList* parent_ = nullptr;
};

