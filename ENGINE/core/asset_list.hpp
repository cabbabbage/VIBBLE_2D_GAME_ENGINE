#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include <SDL.h>

class Asset;

enum class SortMode {
    Unsorted,
    ZIndexAsc,
    ZIndexDesc
};

class AssetList {
public:
    AssetList(const std::vector<Asset*>& source_candidates, SDL_Point list_center, int search_radius, const std::vector<std::string>& required_tags, const std::vector<std::string>& top_bucket_tags, const std::vector<std::string>& bottom_bucket_tags, SortMode sort_mode, std::function<bool(const Asset*)> eligibility_filter = nullptr);

    AssetList(const std::vector<Asset*>& source_candidates, Asset* center_asset, int search_radius, const std::vector<std::string>& required_tags, const std::vector<std::string>& top_bucket_tags, const std::vector<std::string>& bottom_bucket_tags, SortMode sort_mode, std::function<bool(const Asset*)> eligibility_filter = nullptr);

    AssetList(const AssetList& parent_list, SDL_Point list_center, int search_radius, const std::vector<std::string>& required_tags, const std::vector<std::string>& top_bucket_tags, const std::vector<std::string>& bottom_bucket_tags, SortMode sort_mode, std::function<bool(const Asset*)> eligibility_filter = nullptr);

    AssetList(const AssetList& parent_list, Asset* center_asset, int search_radius, const std::vector<std::string>& required_tags, const std::vector<std::string>& top_bucket_tags, const std::vector<std::string>& bottom_bucket_tags, SortMode sort_mode, std::function<bool(const Asset*)> eligibility_filter = nullptr);

    // Variant that allows using the parent's current filtered contents as the candidate set
    // instead of the original source candidates. This avoids re-scanning the entire pool when
    // building a refined child list (e.g., impassable neighbors from general neighbors).
    AssetList(const AssetList& parent_list, SDL_Point list_center, int search_radius, const std::vector<std::string>& required_tags, const std::vector<std::string>& top_bucket_tags, const std::vector<std::string>& bottom_bucket_tags, SortMode sort_mode, std::function<bool(const Asset*)> eligibility_filter, bool inherit_parent_view);

    AssetList(const AssetList& parent_list, Asset* center_asset, int search_radius, const std::vector<std::string>& required_tags, const std::vector<std::string>& top_bucket_tags, const std::vector<std::string>& bottom_bucket_tags, SortMode sort_mode, std::function<bool(const Asset*)> eligibility_filter, bool inherit_parent_view);

    void add_child(std::unique_ptr<AssetList> child);
    const std::vector<std::unique_ptr<AssetList>>& children() const;

    const std::vector<Asset*>& top_unsorted() const;
    const std::vector<Asset*>& middle_sorted() const;
    const std::vector<Asset*>& bottom_unsorted() const;
    void full_list(std::vector<Asset*>& out) const;

    void set_center(SDL_Point p);
    void set_center(Asset* a);
    void set_search_radius(int r);
    void set_sort_mode(SortMode m);
    void set_tags(const std::vector<std::string>& required_tags, const std::vector<std::string>& top_bucket_tags, const std::vector<std::string>& bottom_bucket_tags);

    void update();
    void update(SDL_Point new_center);

    std::vector<Asset*> get_union(const AssetList& other, const std::vector<std::string>& required_tags) const;

    int search_radius() const { return search_radius_; }

private:
    SDL_Point resolve_center() const;
    void rebuild_from_scratch();
    void route_asset_to_section(Asset* a);
    void remove_from_all_sections(Asset* a);
    bool has_all_required_tags(const Asset* a, const std::vector<std::string>& req) const;
    bool has_any_tag(const Asset* a, const std::vector<std::string>& tags) const;
    void sort_middle_section();
    bool is_asset_eligible(const Asset* a) const;

    void get_delta_area_assets(SDL_Point prev_center, int prev_radius, SDL_Point curr_center, int curr_radius, std::vector<Asset*>& out_changed) const;

    void for_each_candidate(const std::function<void(Asset*)>& f) const;

    std::vector<Asset*> source_candidates_;
    SDL_Point           center_point_{};
    Asset*              center_asset_ = nullptr;
    int                 search_radius_ = 0;
    std::vector<std::string> required_tags_;
    std::vector<std::string> top_bucket_tags_;
    std::vector<std::string> bottom_bucket_tags_;
    SortMode            sort_mode_ = SortMode::Unsorted;

    std::vector<Asset*> list_top_unsorted_;
    std::vector<Asset*> list_middle_sorted_;
    std::vector<Asset*> list_bottom_unsorted_;

    std::vector<Asset*> list_always_ineligible_;
    std::unordered_set<Asset*> list_always_ineligible_lookup_;

    std::vector<std::unique_ptr<AssetList>> children_;

    std::function<bool(const Asset*)> eligibility_filter_;

    SDL_Point previous_center_point_{};
    int       previous_search_radius_ = 0;

    std::vector<Asset*> delta_buffer_;
    mutable std::vector<bool> delta_inside_flags_;

    // Optional view of a parent list's current filtered contents as our candidate set.
    const AssetList* parent_provider_ = nullptr;
    bool inherit_parent_view_ = false;
};

