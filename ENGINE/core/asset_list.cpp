#include "core/asset_list.hpp"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace {

template <typename Container>
void remove_from_vector(Container& container, Asset* value) {
    auto it = std::remove(container.begin(), container.end(), value);
    if (it != container.end()) {
        container.erase(it, container.end());
    }
}

}  // namespace

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
      sort_mode_(sort_mode) {
    rebuild_candidate_lookup();
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
      center_asset_(center_asset),
      search_radius_(search_radius),
      required_tags_(required_tags),
      top_bucket_tags_(top_bucket_tags),
      bottom_bucket_tags_(bottom_bucket_tags),
      sort_mode_(sort_mode) {
    if (center_asset_) {
        center_point_ = center_asset_->pos;
    }
    rebuild_candidate_lookup();
    rebuild_from_scratch();
}

AssetList::AssetList(const AssetList& parent_list,
                     SDL_Point list_center,
                     int search_radius,
                     const std::vector<std::string>& required_tags,
                     const std::vector<std::string>& top_bucket_tags,
                     const std::vector<std::string>& bottom_bucket_tags,
                     SortMode sort_mode)
    : center_point_(list_center),
      center_asset_(nullptr),
      search_radius_(search_radius),
      required_tags_(required_tags),
      top_bucket_tags_(top_bucket_tags),
      bottom_bucket_tags_(bottom_bucket_tags),
      sort_mode_(sort_mode) {
    parent_list.full_list(source_candidates_);
    rebuild_candidate_lookup();
    rebuild_from_scratch();
}

AssetList::AssetList(const AssetList& parent_list,
                     Asset* center_asset,
                     int search_radius,
                     const std::vector<std::string>& required_tags,
                     const std::vector<std::string>& top_bucket_tags,
                     const std::vector<std::string>& bottom_bucket_tags,
                     SortMode sort_mode)
    : center_asset_(center_asset),
      search_radius_(search_radius),
      required_tags_(required_tags),
      top_bucket_tags_(top_bucket_tags),
      bottom_bucket_tags_(bottom_bucket_tags),
      sort_mode_(sort_mode) {
    parent_list.full_list(source_candidates_);
    if (center_asset_) {
        center_point_ = center_asset_->pos;
    }
    rebuild_candidate_lookup();
    rebuild_from_scratch();
}

void AssetList::add_child(std::unique_ptr<AssetList> child) {
    if (!child) {
        return;
    }
    child->set_parent(this);
    child->rebuild_from_scratch();
    children_.push_back(std::move(child));
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
    if (center_asset_) {
        center_point_ = center_asset_->pos;
    }
}

void AssetList::set_sort_mode(SortMode m) {
    sort_mode_ = m;
    apply_sort_mode();
}

void AssetList::set_tags(const std::vector<std::string>& required_tags,
                         const std::vector<std::string>& top_bucket_tags,
                         const std::vector<std::string>& bottom_bucket_tags) {
    required_tags_     = required_tags;
    top_bucket_tags_   = top_bucket_tags;
    bottom_bucket_tags_ = bottom_bucket_tags;
    rebuild_from_scratch();
}

void AssetList::update(SDL_Point new_center) {
    set_center(new_center);
    update();
}

void AssetList::update() {
    if (parent_) {
        sync_with_parent_candidates();
    }

    SDL_Point current_center = resolve_center();

    scratch_delta_.clear();
    get_delta_area_assets(previous_center_point_,
                          previous_search_radius_,
                          current_center,
                          search_radius_,
                          source_candidates_,
                          scratch_delta_);

    scratch_membership_set_.clear();
    scratch_membership_set_.reserve(scratch_delta_.size() + pending_force_recheck_.size());
    for (Asset* a : scratch_delta_) {
        scratch_membership_set_.insert(a);
    }
    for (Asset* a : pending_force_recheck_) {
        if (scratch_membership_set_.insert(a).second) {
            scratch_delta_.push_back(a);
        }
    }
    pending_force_recheck_.clear();

    for (Asset* asset : scratch_delta_) {
        if (!asset) {
            continue;
        }

        const bool in_current = Range::is_in_range(current_center, asset, search_radius_);
        if (in_current) {
            if (!has_all_required_tags(asset, required_tags_)) {
                if (always_ineligible_lookup_.insert(asset).second) {
                    list_always_ineligible_.push_back(asset);
                }
                remove_from_all_sections(asset);
                continue;
            }

            remove_from_all_sections(asset);
            route_asset_to_section(asset);
        } else {
            remove_from_all_sections(asset);
        }
    }

    apply_sort_mode();
    previous_center_point_  = current_center;
    previous_search_radius_ = search_radius_;

    for (auto& child : children_) {
        if (child) {
            child->update();
        }
    }
}

std::vector<Asset*> AssetList::get_union(const AssetList& other,
                                         const std::vector<std::string>& required_tags) const {
    std::vector<Asset*> this_assets;
    std::vector<Asset*> other_assets;
    this_assets.reserve(list_top_unsorted_.size() + list_middle_sorted_.size() + list_bottom_unsorted_.size());
    other_assets.reserve(other.list_top_unsorted_.size() + other.list_middle_sorted_.size() +
                         other.list_bottom_unsorted_.size());
    this_assets.clear();
    other_assets.clear();
    full_list(this_assets);
    other.full_list(other_assets);

    std::vector<Asset*> result;
    result.reserve(std::min(this_assets.size(), other_assets.size()));

    std::unordered_set<Asset*> other_lookup(other_assets.begin(), other_assets.end());
    for (Asset* asset : this_assets) {
        if (!asset) {
            continue;
        }
        if (other_lookup.find(asset) == other_lookup.end()) {
            continue;
        }
        if (has_all_required_tags(asset, required_tags)) {
            result.push_back(asset);
        }
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
    if (parent_) {
        sync_with_parent_candidates();
    } else {
        rebuild_candidate_lookup();
    }

    list_top_unsorted_.clear();
    list_middle_sorted_.clear();
    list_bottom_unsorted_.clear();
    list_always_ineligible_.clear();
    always_ineligible_lookup_.clear();
    pending_force_recheck_.clear();

    SDL_Point center = resolve_center();

    for (Asset* asset : source_candidates_) {
        if (!asset) {
            continue;
        }
        if (!has_all_required_tags(asset, required_tags_)) {
            if (always_ineligible_lookup_.insert(asset).second) {
                list_always_ineligible_.push_back(asset);
            }
            continue;
        }
        if (!Range::is_in_range(center, asset, search_radius_)) {
            continue;
        }
        route_asset_to_section(asset);
    }

    apply_sort_mode();

    previous_center_point_  = center;
    previous_search_radius_ = search_radius_;
}

void AssetList::route_asset_to_section(Asset* a) {
    if (!a) {
        return;
    }
    remove_from_all_sections(a);

    if (has_any_tag(a, top_bucket_tags_)) {
        list_top_unsorted_.push_back(a);
        return;
    }
    if (has_any_tag(a, bottom_bucket_tags_)) {
        list_bottom_unsorted_.push_back(a);
        return;
    }
    list_middle_sorted_.push_back(a);
}

void AssetList::remove_from_all_sections(Asset* a) {
    if (!a) {
        return;
    }
    remove_from_vector(list_top_unsorted_, a);
    remove_from_vector(list_middle_sorted_, a);
    remove_from_vector(list_bottom_unsorted_, a);
}

bool AssetList::has_all_required_tags(const Asset* a, const std::vector<std::string>& req) const {
    if (!a || !a->info) {
        return req.empty();
    }
    const auto& tags = a->info->tags;
    for (const auto& tag : req) {
        if (std::find(tags.begin(), tags.end(), tag) == tags.end()) {
            return false;
        }
    }
    return true;
}

bool AssetList::has_any_tag(const Asset* a, const std::vector<std::string>& tags) const {
    if (!a || !a->info) {
        return false;
    }
    const auto& asset_tags = a->info->tags;
    for (const auto& tag : tags) {
        if (std::find(asset_tags.begin(), asset_tags.end(), tag) != asset_tags.end()) {
            return true;
        }
    }
    return false;
}

void AssetList::get_delta_area_assets(SDL_Point prev_center,
                                      int prev_radius,
                                      SDL_Point curr_center,
                                      int curr_radius,
                                      const std::vector<Asset*>& candidates,
                                      std::vector<Asset*>& out_changed) const {
    out_changed.clear();

    if (prev_center.x == curr_center.x && prev_center.y == curr_center.y && prev_radius == curr_radius) {
        return;
    }

    out_changed.reserve(candidates.size());
    for (Asset* asset : candidates) {
        if (!asset) {
            continue;
        }
        if (always_ineligible_lookup_.find(asset) != always_ineligible_lookup_.end()) {
            continue;
        }

        const bool in_prev = Range::is_in_range(prev_center, asset, prev_radius);
        const bool in_curr = Range::is_in_range(curr_center, asset, curr_radius);
        if (in_prev != in_curr) {
            out_changed.push_back(asset);
        }
    }
}

void AssetList::rebuild_candidate_lookup() {
    candidates_lookup_.clear();
    candidates_lookup_.reserve(source_candidates_.size() * 2);
    for (Asset* asset : source_candidates_) {
        if (asset) {
            candidates_lookup_.insert(asset);
        }
    }
}

void AssetList::sync_with_parent_candidates() {
    if (!parent_) {
        return;
    }

    scratch_parent_full_.clear();
    parent_->full_list(scratch_parent_full_);

    std::unordered_set<Asset*> updated_lookup;
    updated_lookup.reserve(scratch_parent_full_.size() * 2);
    for (Asset* asset : scratch_parent_full_) {
        if (!asset) {
            continue;
        }
        if (updated_lookup.insert(asset).second) {
            if (candidates_lookup_.find(asset) == candidates_lookup_.end()) {
                pending_force_recheck_.push_back(asset);
            }
        }
    }

    for (Asset* previous : source_candidates_) {
        if (updated_lookup.find(previous) == updated_lookup.end()) {
            remove_from_all_sections(previous);
            if (always_ineligible_lookup_.erase(previous) > 0) {
                remove_from_vector(list_always_ineligible_, previous);
            }
        }
    }

    source_candidates_.assign(scratch_parent_full_.begin(), scratch_parent_full_.end());
    candidates_lookup_ = std::move(updated_lookup);
    prune_to_candidates();
}

void AssetList::prune_to_candidates() {
    auto should_remove = [&](Asset* asset) {
        return candidates_lookup_.find(asset) == candidates_lookup_.end();
    };

    list_top_unsorted_.erase(
        std::remove_if(list_top_unsorted_.begin(), list_top_unsorted_.end(), should_remove),
        list_top_unsorted_.end());
    list_middle_sorted_.erase(
        std::remove_if(list_middle_sorted_.begin(), list_middle_sorted_.end(), should_remove),
        list_middle_sorted_.end());
    list_bottom_unsorted_.erase(
        std::remove_if(list_bottom_unsorted_.begin(), list_bottom_unsorted_.end(), should_remove),
        list_bottom_unsorted_.end());

    auto ineligible_end = std::remove_if(list_always_ineligible_.begin(),
                                         list_always_ineligible_.end(),
                                         [&](Asset* asset) {
                                             if (should_remove(asset)) {
                                                 always_ineligible_lookup_.erase(asset);
                                                 return true;
                                             }
                                             return false;
                                         });
    list_always_ineligible_.erase(ineligible_end, list_always_ineligible_.end());

    pending_force_recheck_.erase(
        std::remove_if(pending_force_recheck_.begin(), pending_force_recheck_.end(), should_remove),
        pending_force_recheck_.end());
}

void AssetList::apply_sort_mode() {
    if (sort_mode_ == SortMode::Unsorted) {
        return;
    }

    auto comparator = [this](Asset* lhs, Asset* rhs) {
        if (!lhs || !rhs) {
            return lhs < rhs;
        }
        if (sort_mode_ == SortMode::ZIndexAsc) {
            if (lhs->z_index == rhs->z_index) {
                return lhs < rhs;
            }
            return lhs->z_index < rhs->z_index;
        }
        if (lhs->z_index == rhs->z_index) {
            return lhs < rhs;
        }
        return lhs->z_index > rhs->z_index;
    };

    std::sort(list_middle_sorted_.begin(), list_middle_sorted_.end(), comparator);
}
