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

        for (Asset* a : active_assets_) {
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
	auto it = std::lower_bound(
	active_assets_.begin(), active_assets_.end(), asset,
	[](Asset* A, Asset* B) { return A->z_index < B->z_index; });
	active_assets_.insert(it, asset);

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
	auto it = std::remove(active_assets_.begin(), active_assets_.end(), asset);
        active_assets_.erase(it, active_assets_.end());
}

void ActiveAssetsManager::updateActiveAssets(int cx, int cy)
{
        if (!all_assets_) return;

        std::vector<Asset*> prev_active;
        prev_active.swap(active_assets_);

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
        std::sort(active_assets_.begin(), active_assets_.end(),
        [](Asset* A, Asset* B) {
                if (A->z_index != B->z_index) return A->z_index < B->z_index;
                if (A->pos.y != B->pos.y)     return A->pos.y < B->pos.y;
                if (A->pos.x != B->pos.x)     return A->pos.x < B->pos.x;
           return A < B;
           });
        needs_sort_ = false;
}

void ActiveAssetsManager::addActiveUnsorted(Asset* asset)
{
        if (!asset || asset->active) return;
        asset->active = true;
        active_assets_.push_back(asset);

        for (Asset* c : asset->children) {
                if (c && !c->dead && c->info) {
                        addActiveUnsorted(c);
                }
        }
}
