#include "core/asset_list.hpp"

#include <algorithm>
#include <cstddef>
#include <unordered_set>

#include "asset/Asset.hpp"
#include "utils/range_util.hpp"

namespace {
    bool contains_asset(const std::unordered_set<Asset*>& lookup, Asset* asset) {
        return lookup.find(asset) != lookup.end();
    }
}

AssetList::AssetList(const std::vector<Asset*>& source_candidates,
                     SDL_Point list_center,
                     int search_radius,
                     const std::vector<std::string>& required_tags,
                     const std::vector<std::string>& top_bucket_tags,
                     const std::vector<std::string>& bottom_bucket_tags,
                     SortMode sort_mode)
    : source_candidates_(source_candidates),
      center_point_(list_center),
      center_asset_(nullptr),
      search_radius_(search_radius),
      required_tags_(required_tags),
      top_bucket_tags_(top_bucket_tags),
      bottom_bucket_tags_(bottom_bucket_tags),
      sort_mode_(sort_mode),
      previous_center_point_(list_center),
      previous_search_radius_(search_radius) {
    rebuild_from_scratch();
}

AssetList::AssetList(const std::vector<Asset*>& source_candidates,
                     Asset* center_asset,
                     int search_radius,
                     const std::vector<std::string>& required_tags,
                     const std::vector<std::string>& top_bucket_tags,
                     const std::vector<std::string>& bottom_bucket_tags,
                     SortMode sort_mode)
    : source_candidates_(source_candidates),
      center_point_(center_asset ? center_asset->pos : SDL_Point{0, 0}),
      center_asset_(center_asset),
      search_radius_(search_radius),
      required_tags_(required_tags),
      top_bucket_tags_(top_bucket_tags),
      bottom_bucket_tags_(bottom_bucket_tags),
      sort_mode_(sort_mode),
      previous_center_point_(resolve_center()),
      previous_search_radius_(search_radius) {
    rebuild_from_scratch();
}

AssetList::AssetList(const AssetList& parent_list,
                     SDL_Point list_center,
                     int search_radius,
                     const std::vector<std::string>& required_tags,
                     const std::vector<std::string>& top_bucket_tags,
                     const std::vector<std::string>& bottom_bucket_tags,
                     SortMode sort_mode)
    : source_candidates_(parent_list.source_candidates_),
      center_point_(list_center),
      center_asset_(nullptr),
      search_radius_(search_radius),
      required_tags_(required_tags),
      top_bucket_tags_(top_bucket_tags),
      bottom_bucket_tags_(bottom_bucket_tags),
      sort_mode_(sort_mode),
      previous_center_point_(list_center),
      previous_search_radius_(search_radius) {
    rebuild_from_scratch();
}

AssetList::AssetList(const AssetList& parent_list,
                     Asset* center_asset,
                     int search_radius,
                     const std::vector<std::string>& required_tags,
                     const std::vector<std::string>& top_bucket_tags,
                     const std::vector<std::string>& bottom_bucket_tags,
                     SortMode sort_mode)
    : source_candidates_(parent_list.source_candidates_),
      center_point_(center_asset ? center_asset->pos : SDL_Point{0, 0}),
      center_asset_(center_asset),
      search_radius_(search_radius),
      required_tags_(required_tags),
      top_bucket_tags_(top_bucket_tags),
      bottom_bucket_tags_(bottom_bucket_tags),
      sort_mode_(sort_mode),
      previous_center_point_(resolve_center()),
      previous_search_radius_(search_radius) {
    rebuild_from_scratch();
}

void AssetList::add_child(std::unique_ptr<AssetList> child) {
    if (child) {
        children_.push_back(std::move(child));
    }
}

const std::vector<std::unique_ptr<AssetList>>& AssetList::children() const {
    return children_;
}

const std::vector<Asset*>& AssetList::top_unsorted() const {
    return list_top_unsorted_;
}

const std::vector<Asset*>& AssetList::middle_sorted() const {
    return list_middle_sorted_;
}

const std::vector<Asset*>& AssetList::bottom_unsorted() const {
    return list_bottom_unsorted_;
}

void AssetList::full_list(std::vector<Asset*>& out) const {
    out.insert(out.end(), list_top_unsorted_.begin(), list_top_unsorted_.end());
    out.insert(out.end(), list_middle_sorted_.begin(), list_middle_sorted_.end());
    out.insert(out.end(), list_bottom_unsorted_.begin(), list_bottom_unsorted_.end());
}

void AssetList::set_center(SDL_Point p) {
    center_point_ = p;
    center_asset_ = nullptr;
}

void AssetList::set_center(Asset* a) {
    center_asset_ = a;
    if (a) {
        center_point_ = a->pos;
    }
}

void AssetList::set_search_radius(int r) {
    search_radius_ = r;
}

void AssetList::set_sort_mode(SortMode m) {
    sort_mode_ = m;
    sort_middle_section();
}

void AssetList::set_tags(const std::vector<std::string>& required_tags,
                         const std::vector<std::string>& top_bucket_tags,
                         const std::vector<std::string>& bottom_bucket_tags) {
    required_tags_ = required_tags;
    top_bucket_tags_ = top_bucket_tags;
    bottom_bucket_tags_ = bottom_bucket_tags;
    rebuild_from_scratch();
}

void AssetList::update() {
    SDL_Point current_center = resolve_center();

    delta_buffer_.clear();
    delta_inside_flags_.clear();
    get_delta_area_assets(previous_center_point_,
                          previous_search_radius_,
                          current_center,
                          search_radius_,
                          source_candidates_,
                          delta_buffer_);

    for (std::size_t i = 0; i < delta_buffer_.size(); ++i) {
        Asset* asset = delta_buffer_[i];
        if (asset == nullptr) {
            continue;
        }

        bool now_inside = (i < delta_inside_flags_.size()) ? delta_inside_flags_[i] : Range::is_in_range(current_center, asset, search_radius_);
        if (now_inside) {
            if (!has_all_required_tags(asset, required_tags_)) {
                if (!contains_asset(list_always_ineligible_lookup_, asset)) {
                    list_always_ineligible_.push_back(asset);
                    list_always_ineligible_lookup_.insert(asset);
                }
                remove_from_all_sections(asset);
                continue;
            }

            route_asset_to_section(asset);
        } else {
            remove_from_all_sections(asset);
        }
    }

    sort_middle_section();

    previous_center_point_ = current_center;
    previous_search_radius_ = search_radius_;

    for (const auto& child : children_) {
        if (child) {
            child->update();
        }
    }
}

void AssetList::update(SDL_Point new_center) {
    set_center(new_center);
    update();
}

std::vector<Asset*> AssetList::get_union(const AssetList& other,
                                         const std::vector<std::string>& required_tags) const {
    std::vector<Asset*> result;
    result.reserve(list_top_unsorted_.size() + list_middle_sorted_.size() + list_bottom_unsorted_.size());

    std::unordered_set<Asset*> other_assets;
    other_assets.reserve(other.list_top_unsorted_.size() +
                         other.list_middle_sorted_.size() +
                         other.list_bottom_unsorted_.size());

    for (Asset* asset : other.list_top_unsorted_) {
        other_assets.insert(asset);
    }
    for (Asset* asset : other.list_middle_sorted_) {
        other_assets.insert(asset);
    }
    for (Asset* asset : other.list_bottom_unsorted_) {
        other_assets.insert(asset);
    }

    auto consider = [&](Asset* asset) {
        if (asset && other_assets.find(asset) != other_assets.end() &&
            has_all_required_tags(asset, required_tags)) {
            result.push_back(asset);
        }
    };

    for (Asset* asset : list_top_unsorted_) {
        consider(asset);
    }
    for (Asset* asset : list_middle_sorted_) {
        consider(asset);
    }
    for (Asset* asset : list_bottom_unsorted_) {
        consider(asset);
    }

    return result;
}

SDL_Point AssetList::resolve_center() const {
    if (center_asset_) {
        return center_asset_->pos;
    }
    return center_point_;
}

void AssetList::rebuild_from_scratch() {
    list_top_unsorted_.clear();
    list_middle_sorted_.clear();
    list_bottom_unsorted_.clear();
    list_always_ineligible_.clear();
    list_always_ineligible_lookup_.clear();
    delta_buffer_.clear();
    delta_inside_flags_.clear();

    SDL_Point center = resolve_center();

    auto process_asset = [&](auto&& self, Asset* asset) -> void {
        if (asset == nullptr) {
            return;
        }

        if (!has_all_required_tags(asset, required_tags_)) {
            if (!contains_asset(list_always_ineligible_lookup_, asset)) {
                list_always_ineligible_.push_back(asset);
                list_always_ineligible_lookup_.insert(asset);
            }
        } else if (Range::is_in_range(center, asset, search_radius_)) {
            route_asset_to_section(asset);
        }

        for (Asset* child : asset->children) {
            self(self, child);
        }
    };

    for (Asset* asset : source_candidates_) {
        process_asset(process_asset, asset);
    }

    sort_middle_section();

    previous_center_point_ = center;
    previous_search_radius_ = search_radius_;
}

void AssetList::route_asset_to_section(Asset* a) {
    if (a == nullptr) {
        return;
    }

    if (contains_asset(list_always_ineligible_lookup_, a)) {
        return;
    }

    remove_from_all_sections(a);

    if (!top_bucket_tags_.empty() && has_any_tag(a, top_bucket_tags_)) {
        list_top_unsorted_.push_back(a);
        return;
    }

    if (!bottom_bucket_tags_.empty() && has_any_tag(a, bottom_bucket_tags_)) {
        list_bottom_unsorted_.push_back(a);
        return;
    }

    list_middle_sorted_.push_back(a);
}

void AssetList::remove_from_all_sections(Asset* a) {
    if (a == nullptr) {
        return;
    }

    auto remover = [a](std::vector<Asset*>& vec) {
        vec.erase(std::remove(vec.begin(), vec.end(), a), vec.end());
    };

    remover(list_top_unsorted_);
    remover(list_middle_sorted_);
    remover(list_bottom_unsorted_);
}

bool AssetList::has_all_required_tags(const Asset* a, const std::vector<std::string>& req) const {
    if (a == nullptr || !a->info) {
        return false;
    }

    const auto& asset_tags = a->info->tags;
    for (const std::string& tag : req) {
        if (std::find(asset_tags.begin(), asset_tags.end(), tag) == asset_tags.end()) {
            return false;
        }
    }
    return true;
}

bool AssetList::has_any_tag(const Asset* a, const std::vector<std::string>& tags) const {
    if (a == nullptr || !a->info) {
        return false;
    }

    if (tags.empty()) {
        return false;
    }

    const auto& asset_tags = a->info->tags;
    for (const std::string& tag : tags) {
        if (std::find(asset_tags.begin(), asset_tags.end(), tag) != asset_tags.end()) {
            return true;
        }
    }
    return false;
}

void AssetList::sort_middle_section() {
    switch (sort_mode_) {
        case SortMode::Unsorted:
            break;
        case SortMode::ZIndexAsc:
            std::sort(list_middle_sorted_.begin(), list_middle_sorted_.end(), [](const Asset* lhs, const Asset* rhs) {
                if (lhs == nullptr || rhs == nullptr) {
                    return rhs != nullptr;
                }
                if (lhs->z_index == rhs->z_index) {
                    return lhs < rhs;
                }
                return lhs->z_index < rhs->z_index;
            });
            break;
        case SortMode::ZIndexDesc:
            std::sort(list_middle_sorted_.begin(), list_middle_sorted_.end(), [](const Asset* lhs, const Asset* rhs) {
                if (lhs == nullptr || rhs == nullptr) {
                    return lhs != nullptr;
                }
                if (lhs->z_index == rhs->z_index) {
                    return lhs > rhs;
                }
                return lhs->z_index > rhs->z_index;
            });
            break;
    }
}

void AssetList::get_delta_area_assets(SDL_Point prev_center,
                                      int prev_radius,
                                      SDL_Point curr_center,
                                      int curr_radius,
                                      const std::vector<Asset*>& candidates,
                                      std::vector<Asset*>& out_changed) const {
    auto evaluate_asset = [&](auto&& self, Asset* asset) -> void {
        if (asset == nullptr) {
            return;
        }

        if (!contains_asset(list_always_ineligible_lookup_, asset)) {
            bool was_inside = Range::is_in_range(prev_center, asset, prev_radius);
            bool now_inside = Range::is_in_range(curr_center, asset, curr_radius);
            if (was_inside != now_inside) {
                out_changed.push_back(asset);
                delta_inside_flags_.push_back(now_inside);
            }
        }

        for (Asset* child : asset->children) {
            self(self, child);
        }
    };

    for (Asset* asset : candidates) {
        evaluate_asset(evaluate_asset, asset);
    }
}

