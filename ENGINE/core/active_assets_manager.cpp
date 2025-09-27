#include "active_assets_manager.hpp"
#include <algorithm>

ActiveAssetsManager::ActiveAssetsManager(int screen_width, int screen_height, camera& v)
: camera_(v),
screen_width_(screen_width),
screen_height_(screen_height),
all_assets_(nullptr)
{}

void ActiveAssetsManager::initialize(std::vector<Asset*>& all_assets,
                                     Asset* player,
                                     int screen_center_x,
                                     int screen_center_y)
{
	all_assets_ = &all_assets;
	active_assets_.clear();
	closest_assets_.clear();
	impassable_assets_.clear();
	updateActiveAssets(screen_center_x, screen_center_y);
	if (player) activate(player);
	updateClosestAssets(player, 3);
	sortByZIndex();
}

void ActiveAssetsManager::updateAssetVectors(Asset* player,
                                             int screen_center_x,
                                             int screen_center_y)
{
	if (++activate_counter_ >= update_activate_interval) {
		updateActiveAssets(screen_center_x, screen_center_y);
		activate_counter_ = 0;
	}
	if (++closest_counter_ >= update_closest_interval) {
		updateClosestAssets(player, 3);
		closest_counter_ = 0;
	}
        sortByZIndex();
}

void ActiveAssetsManager::markNeedsSort() {
        needs_sort_ = true;
}

void ActiveAssetsManager::updateClosestAssets(Asset* player, std::size_t max_count)
{
	for (auto* a : closest_assets_) {
		if (a) a->set_render_player_light(false);
	}
	closest_assets_.clear();
	impassable_assets_.clear();

	if (!player || active_assets_.empty() || max_count == 0) return;

        struct DistAsset {
                double dist2;
                Asset* asset;
        };

        const double px = static_cast<double>(player->pos.x);
        const double py = static_cast<double>(player->pos.y);

        std::vector<DistAsset> best;
        best.reserve(std::min<std::size_t>(max_count, active_assets_.size()));

        auto insert_sorted = [&best](DistAsset entry) {
                auto it = std::upper_bound(best.begin(), best.end(), entry.dist2,
                [](double lhs, const DistAsset& rhs) { return lhs < rhs.dist2; });
                best.insert(it, entry);
        };

        // Distance does not depend on sort; iterate both buckets.
        for (Asset* a : textures_) {
                if (!a || a == player) continue;
                const double dx = static_cast<double>(a->pos.x) - px;
                const double dy = static_cast<double>(a->pos.y) - py;
                const double dist2 = dx * dx + dy * dy;

                if (best.size() < max_count) {
                        insert_sorted({dist2, a});
                } else if (!best.empty() && dist2 < best.back().dist2) {
                        insert_sorted({dist2, a});
                        best.pop_back();
                }
        }
        for (Asset* a : others_) {
                if (!a || a == player) continue;
                const double dx = static_cast<double>(a->pos.x) - px;
                const double dy = static_cast<double>(a->pos.y) - py;
                const double dist2 = dx * dx + dy * dy;

                if (best.size() < max_count) {
                        insert_sorted({dist2, a});
                } else if (!best.empty() && dist2 < best.back().dist2) {
                        insert_sorted({dist2, a});
                        best.pop_back();
                }
        }

        closest_assets_.reserve(best.size());
        for (const DistAsset& entry : best) {
                closest_assets_.push_back(entry.asset);
        }

	for (Asset* a : closest_assets_) {
		if (!a) continue;
		a->set_render_player_light(true);
		if (a->info && !a->info->passable) {
			impassable_assets_.push_back(a);
		}
	}
}

void ActiveAssetsManager::activate(Asset* asset)
{
        if (!asset || asset->active) return;
        asset->active = true;

        if (is_texture(asset)) {
                textures_.push_back(asset);
        } else if (!needs_sort_) {
                auto it = std::lower_bound(others_.begin(), others_.end(), asset,
                                           [](Asset* A, Asset* B) { return A->z_index < B->z_index; });
                others_.insert(it, asset);
        } else {
                others_.push_back(asset);
        }
        combined_dirty_ = true;

        for (Asset* c : asset->children) {
                if (c && !c->dead && c->info) {
                        activate(c);
                }
        }
}

void ActiveAssetsManager::remove(Asset* asset)
{
        if (!asset || !asset->active) return;
        asset->active = false;

        if (is_texture(asset)) {
                auto it = std::find(textures_.begin(), textures_.end(), asset);
                if (it != textures_.end()) textures_.erase(it);
        } else {
                auto it = std::find(others_.begin(), others_.end(), asset);
                if (it != others_.end()) others_.erase(it);
        }
        combined_dirty_ = true;
}

void ActiveAssetsManager::updateActiveAssets(int cx, int cy)
{
        if (!all_assets_) return;

        // Snapshot currently active assets (both buckets) to detect deactivations.
        std::vector<Asset*> prev_active;
        prev_active.reserve(textures_.size() + others_.size());
        prev_active.insert(prev_active.end(), textures_.begin(), textures_.end());
        prev_active.insert(prev_active.end(), others_.begin(), others_.end());

        for (Asset* a : prev_active) {
                if (a) a->active = false;
        }

        // Use camera's current view area directly, then expand by a fixed on-screen margin
        auto [left, top, right, bottom] = camera_.get_current_view().get_bounds();
        const int buffer_world = camera_.get_render_distance_world_margin();
        left   -= buffer_world;
        right  += buffer_world;
        top    -= buffer_world;
        bottom += buffer_world;

        textures_.clear();
        others_.clear();
        combined_dirty_ = true;

        for (Asset* a : *all_assets_) {
                if (!a) continue;
                const int ax = a->pos.x;
                const int ay = a->pos.y;
                if (ax >= left && ax <= right && ay >= top && ay <= bottom) {
                        addActiveUnsorted(a);
                }
        }

        for (Asset* old_a : prev_active) {
                if (old_a && !old_a->active) {
                        old_a->deactivate();
                }
        }

        needs_sort_ = true;
}

void ActiveAssetsManager::sortByZIndex()
{
        if (!needs_sort_) return;

        // Textures bucket remains unsorted.
        std::sort(others_.begin(), others_.end(),
        [](Asset* A, Asset* B) {
                if (A->z_index != B->z_index) return A->z_index < B->z_index;
                if (A->pos.y != B->pos.y)     return A->pos.y < B->pos.y;
                if (A->pos.x != B->pos.x)     return A->pos.x < B->pos.x;
                return A < B;
        });

        needs_sort_ = false;
        combined_dirty_ = true;
}

void ActiveAssetsManager::addActiveUnsorted(Asset* asset)
{
        if (!asset || asset->active) return;
        asset->active = true;
        if (is_texture(asset)) {
                textures_.push_back(asset);
        } else {
                others_.push_back(asset);
        }
        combined_dirty_ = true;

        for (Asset* c : asset->children) {
                if (c && !c->dead && c->info) {
                        addActiveUnsorted(c);
                }
        }
}

bool ActiveAssetsManager::is_texture(const Asset* a) {
        return a && a->info && a->info->type == "texture";
}

void ActiveAssetsManager::rebuild_combined_if_needed() const {
        if (!combined_dirty_) return;
        active_assets_.clear();
        active_assets_.reserve(textures_.size() + others_.size());
        active_assets_.insert(active_assets_.end(), textures_.begin(), textures_.end());
        active_assets_.insert(active_assets_.end(), others_.begin(), others_.end());
        combined_dirty_ = false;
}

const std::vector<Asset*>& ActiveAssetsManager::getActive() const {
        const_cast<ActiveAssetsManager*>(this)->sortByZIndex();
        rebuild_combined_if_needed();
        return active_assets_;
}
