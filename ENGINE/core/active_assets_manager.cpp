#include "active_assets_manager.hpp"
#include <algorithm>
#include <queue>
#include <cmath>
ActiveAssetsManager::ActiveAssetsManager(int screen_width, int screen_height, view& v)
: view_(v),
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
	interactive_assets_.clear();
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
	interactive_assets_.clear();
	if (!player || active_assets_.empty() || max_count == 0) return;
	struct Pair { float d2; Asset* a; };
	auto cmp = [](const Pair& L, const Pair& R){ return L.d2 < R.d2; };
	std::priority_queue<Pair, std::vector<Pair>, decltype(cmp)> heap(cmp);
	const int px = player->pos_X;
	const int py = player->pos_Y;
	for (Asset* a : active_assets_) {
		if (!a || a == player) continue;
		const float dx = float(a->pos_X - px);
		const float dy = float(a->pos_Y - py);
		const float d2 = dx*dx + dy*dy;
		if (heap.size() < max_count) {
			heap.push({d2, a});
		} else if (d2 < heap.top().d2) {
			heap.pop();
			heap.push({d2, a});
		}
	}
	closest_assets_.reserve(heap.size());
	while (!heap.empty()) {
		closest_assets_.push_back(heap.top().a);
		heap.pop();
	}
	std::sort(closest_assets_.begin(), closest_assets_.end(),
	[&](Asset* A, Asset* B){
		float dAx = float(A->pos_X - px), dAy = float(A->pos_Y - py);
		float dBx = float(B->pos_X - px), dBy = float(B->pos_Y - py);
           return dAx*dAx + dAy*dAy < dBx*dBx + dBy*dBy;
           });
	for (Asset* a : closest_assets_) {
		if (!a) continue;
		a->set_render_player_light(true);
		if (a->info) {
			if (!a->info->passable)
			impassable_assets_.push_back(a);
			if (a->info->find_area("interaction_area"))
			interactive_assets_.push_back(a);
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
        for (Asset* a : *all_assets_) {
                if (a && view_.is_asset_in_bounds(*a, cx, cy)) {
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
                if (A->pos_Y != B->pos_Y)     return A->pos_Y < B->pos_Y;
                if (A->pos_X != B->pos_X)     return A->pos_X < B->pos_X;
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
