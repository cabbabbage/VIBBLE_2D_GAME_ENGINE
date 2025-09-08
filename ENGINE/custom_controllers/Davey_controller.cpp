#include "Davey_controller.hpp"
#include "utils/input.hpp"
#include "asset/Asset.hpp"
#include "asset/auto_movement.hpp"
#include "utils/area.hpp"
#include "core/active_assets_manager.hpp"
#include "core/AssetsManager.hpp"
#include <cmath>
#include <iostream>
#include <random>
#include <algorithm>
DaveyController::DaveyController(Assets* assets, Asset* self, ActiveAssetsManager& aam)
: assets_(assets), self_(self), aam_(aam), mover_(self, aam, true) {}

void DaveyController::update(const Input& ) {
	bool updated_by_determine = false;
	if (!self_ || !assets_ || !self_->info) return;
	Asset* player = assets_->player;
	if (!player) {
		self_->update_animation_manager();
		return;
	}
	long long dx = static_cast<long long>(player->pos_X) - static_cast<long long>(self_->pos_X);
	long long dy = static_cast<long long>(player->pos_Y) - static_cast<long long>(self_->pos_Y);
	long long d2 = dx*dx + dy*dy;
	const long long r = 1000LL;
	if (d2 <= r*r) {
		constexpr int radius = 30;
		constexpr double pi = 3.14159265358979323846;
		static thread_local std::mt19937 rng{std::random_device{}()};
		if (pursue_frames_left_ <= 0) {
			std::uniform_real_distribution<double> angle_dist(0.0, 2.0 * pi);
			double theta = angle_dist(rng);
			pursue_target_x_ = player->pos_X + static_cast<int>(std::llround(radius * std::cos(theta)));
			pursue_target_y_ = player->pos_Y + static_cast<int>(std::llround(radius * std::sin(theta)));
			pursue_frames_left_ = pursue_recalc_interval_;
		} else {
			pursue_frames_left_ -= 1;
		}
		int target_x = pursue_target_x_;
		int target_y = pursue_target_y_;
		mover_.set_pursue(player, 20, 30);
		mover_.move();
		updated_by_determine = true;
	} else {
	}
	if (!updated_by_determine) self_->update_animation_manager();
}
