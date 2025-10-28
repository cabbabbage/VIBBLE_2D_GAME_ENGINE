#include "core/asset_list.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
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
                     SortMode sort_mode,
                     std::function<bool(const Asset*)> eligibility_filter)
    : source_candidates_(source_candidates),
      center_point_(list_center),
      center_asset_(nullptr),
      search_radius_(search_radius),
      required_tags_(required_tags),
      top_bucket_tags_(top_bucket_tags),
      bottom_bucket_tags_(bottom_bucket_tags),
      sort_mode_(sort_mode),
      eligibility_filter_(std::move(eligibility_filter)),
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
                     SortMode sort_mode,
                     std::function<bool(const Asset*)> eligibility_filter)
    : source_candidates_(source_candidates),
      center_point_(center_asset ? center_asset->pos : SDL_Point{0, 0}),
      center_asset_(center_asset),
      search_radius_(search_radius),
      required_tags_(required_tags),
      top_bucket_tags_(top_bucket_tags),
      bottom_bucket_tags_(bottom_bucket_tags),
      sort_mode_(sort_mode),
      eligibility_filter_(std::move(eligibility_filter)),
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
                     SortMode sort_mode,
                     std::function<bool(const Asset*)> eligibility_filter)
    : source_candidates_(parent_list.source_candidates_),
      center_point_(list_center),
      center_asset_(nullptr),
      search_radius_(search_radius),
      required_tags_(required_tags),
      top_bucket_tags_(top_bucket_tags),
      bottom_bucket_tags_(bottom_bucket_tags),
      sort_mode_(sort_mode),
      eligibility_filter_(std::move(eligibility_filter)),
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
                     SortMode sort_mode,
                     std::function<bool(const Asset*)> eligibility_filter)
    : source_candidates_(parent_list.source_candidates_),
      center_point_(center_asset ? center_asset->pos : SDL_Point{0, 0}),
      center_asset_(center_asset),
      search_radius_(search_radius),
      required_tags_(required_tags),
      top_bucket_tags_(top_bucket_tags),
      bottom_bucket_tags_(bottom_bucket_tags),
      sort_mode_(sort_mode),
      eligibility_filter_(std::move(eligibility_filter)),
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
                     SortMode sort_mode,
                     std::function<bool(const Asset*)> eligibility_filter,
                     bool inherit_parent_view)
    : source_candidates_(parent_list.source_candidates_),
      center_point_(list_center),
      center_asset_(nullptr),
      search_radius_(search_radius),
      required_tags_(required_tags),
      top_bucket_tags_(top_bucket_tags),
      bottom_bucket_tags_(bottom_bucket_tags),
      sort_mode_(sort_mode),
      eligibility_filter_(std::move(eligibility_filter)),
      previous_center_point_(list_center),
      previous_search_radius_(search_radius),
      parent_provider_(&parent_list),
      inherit_parent_view_(inherit_parent_view) {
    rebuild_from_scratch();
}

AssetList::AssetList(const AssetList& parent_list,
                     Asset* center_asset,
                     int search_radius,
                     const std::vector<std::string>& required_tags,
                     const std::vector<std::string>& top_bucket_tags,
                     const std::vector<std::string>& bottom_bucket_tags,
                     SortMode sort_mode,
                     std::function<bool(const Asset*)> eligibility_filter,
                     bool inherit_parent_view)
    : source_candidates_(parent_list.source_candidates_),
      center_point_(center_asset ? center_asset->pos : SDL_Point{0, 0}),
      center_asset_(center_asset),
      search_radius_(search_radius),
      required_tags_(required_tags),
      top_bucket_tags_(top_bucket_tags),
      bottom_bucket_tags_(bottom_bucket_tags),
      sort_mode_(sort_mode),
      eligibility_filter_(std::move(eligibility_filter)),
      previous_center_point_(resolve_center()),
      previous_search_radius_(search_radius),
      parent_provider_(&parent_list),
      inherit_parent_view_(inherit_parent_view) {
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
    middle_section_dirty_ = true;
    if (middle_section_dirty_) {
        sort_middle_section();
    }
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
    get_delta_area_assets(previous_center_point_, previous_search_radius_, current_center, search_radius_, delta_buffer_);

    for (std::size_t i = 0; i < delta_buffer_.size(); ++i) {
        Asset* asset = delta_buffer_[i];
        if (asset == nullptr) {
            continue;
        }

        if (!is_asset_eligible(asset)) {
            if (!contains_asset(list_always_ineligible_lookup_, asset)) {
                list_always_ineligible_.push_back(asset);
                list_always_ineligible_lookup_.insert(asset);
            }
            remove_from_all_sections(asset);
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

    if (middle_section_dirty_) {
        sort_middle_section();
    }

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
    other_assets.reserve(other.list_top_unsorted_.size() + other.list_middle_sorted_.size() + other.list_bottom_unsorted_.size());

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
    membership_lookup_.clear();
    middle_section_dirty_ = false;

    SDL_Point center = resolve_center();

    for_each_candidate([&](Asset* asset) {
        if (asset == nullptr) {
            return;
        }

        if (!is_asset_eligible(asset)) {
            if (!contains_asset(list_always_ineligible_lookup_, asset)) {
                list_always_ineligible_.push_back(asset);
                list_always_ineligible_lookup_.insert(asset);
            }
            return;
        }

        if (!has_all_required_tags(asset, required_tags_)) {
            if (!contains_asset(list_always_ineligible_lookup_, asset)) {
                list_always_ineligible_.push_back(asset);
                list_always_ineligible_lookup_.insert(asset);
            }
            return;
        }

        if (Range::is_in_range(center, asset, search_radius_)) {
            route_asset_to_section(asset);
        }
    });

    if (middle_section_dirty_) {
        sort_middle_section();
    }

    previous_center_point_ = center;
    previous_search_radius_ = search_radius_;
}

void AssetList::route_asset_to_section(Asset* a) {
    if (a == nullptr) {
        return;
    }

    if (!is_asset_eligible(a)) {
        return;
    }

    if (contains_asset(list_always_ineligible_lookup_, a)) {
        return;
    }

    remove_from_all_sections(a);

    auto place_in_bucket = [&](SectionBucket bucket, std::vector<Asset*>& container) {
        std::size_t index = container.size();
        container.push_back(a);
        membership_lookup_[a] = SectionSlot{bucket, index};
        if (bucket == SectionBucket::Middle) {
            middle_section_dirty_ = true;
        }
};

    if (!top_bucket_tags_.empty() && has_any_tag(a, top_bucket_tags_)) {
        place_in_bucket(SectionBucket::Top, list_top_unsorted_);
        return;
    }

    if (!bottom_bucket_tags_.empty() && has_any_tag(a, bottom_bucket_tags_)) {
        place_in_bucket(SectionBucket::Bottom, list_bottom_unsorted_);
        return;
    }

    place_in_bucket(SectionBucket::Middle, list_middle_sorted_);
}

void AssetList::remove_from_all_sections(Asset* a) {
    if (a == nullptr) {
        return;
    }

    auto it = membership_lookup_.find(a);
    if (it == membership_lookup_.end()) {
        return;
    }

    SectionSlot slot = it->second;
    std::vector<Asset*>& vec = bucket_vector(slot.bucket);

    if (vec.empty()) {
        membership_lookup_.erase(it);
        return;
    }

    if (slot.index >= vec.size() || vec[slot.index] != a) {

        auto found = std::find(vec.begin(), vec.end(), a);
        if (found == vec.end()) {
            membership_lookup_.erase(it);
            return;
        }
        slot.index = static_cast<std::size_t>(std::distance(vec.begin(), found));
        it->second.index = slot.index;
    }

    bool mark_dirty = (slot.bucket == SectionBucket::Middle);

    std::size_t last_index = vec.size() - 1;
    if (slot.index != last_index) {
        Asset* moved = vec[last_index];
        vec[slot.index] = moved;
        vec.pop_back();
        auto moved_it = membership_lookup_.find(moved);
        if (moved_it != membership_lookup_.end()) {
            moved_it->second.index = slot.index;
        }
    } else {
        vec.pop_back();
    }

    membership_lookup_.erase(it);

    if (mark_dirty) {
        middle_section_dirty_ = true;
    }
}

bool AssetList::has_all_required_tags(const Asset* a, const std::vector<std::string>& req) const {
    if (a == nullptr || !a->info) {
        return false;
    }

    const auto& asset_tags = a->info->tag_lookup();
    for (const std::string& tag : req) {
        if (asset_tags.find(tag) == asset_tags.end()) {
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

    const auto& asset_tags = a->info->tag_lookup();
    for (const std::string& tag : tags) {
        if (asset_tags.find(tag) != asset_tags.end()) {
            return true;
        }
    }
    return false;
}

bool AssetList::is_asset_eligible(const Asset* a) const {
    if (!eligibility_filter_) {
        return a != nullptr;
    }
    return a != nullptr && eligibility_filter_(a);
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

    for (std::size_t i = 0; i < list_middle_sorted_.size(); ++i) {
        Asset* asset = list_middle_sorted_[i];
        if (asset) {
            auto it = membership_lookup_.find(asset);
            if (it != membership_lookup_.end()) {
                it->second.index = i;
            }
        }
    }

    middle_section_dirty_ = false;
}

void AssetList::get_delta_area_assets(SDL_Point prev_center,
                                      int prev_radius,
                                      SDL_Point curr_center,
                                      int curr_radius,
                                      std::vector<Asset*>& out_changed) const {
    for_each_candidate([&](Asset* asset) {
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
    });
}

void AssetList::for_each_candidate(const std::function<void(Asset*)>& f) const {
    if (!f) return;
    auto process_asset = [&](auto&& self, Asset* asset) -> void {
        if (asset == nullptr) return;
        f(asset);
        for (Asset* asset_child : asset->asset_children) {
            self(self, asset_child);
        }
};

    if (inherit_parent_view_ && parent_provider_) {
        for (Asset* a : parent_provider_->top_unsorted()) {
            process_asset(process_asset, a);
        }
        for (Asset* a : parent_provider_->middle_sorted()) {
            process_asset(process_asset, a);
        }
        for (Asset* a : parent_provider_->bottom_unsorted()) {
            process_asset(process_asset, a);
        }
    } else {
        for (Asset* a : source_candidates_) {
            process_asset(process_asset, a);
        }
    }
}

std::vector<Asset*>& AssetList::bucket_vector(SectionBucket bucket) {
    switch (bucket) {
        case SectionBucket::Top:
            return list_top_unsorted_;
        case SectionBucket::Middle:
            return list_middle_sorted_;
        case SectionBucket::Bottom:
            return list_bottom_unsorted_;
    }
    return list_middle_sorted_;
}

