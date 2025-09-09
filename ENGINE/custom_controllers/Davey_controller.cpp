#include "Davey_controller.hpp"
#include "utils/input.hpp"
#include "asset/Asset.hpp"
#include "asset/animation_update.hpp"
#include "utils/area.hpp"
#include "core/active_assets_manager.hpp"
#include "core/AssetsManager.hpp"
#include "utils/range_util.hpp"
#include <cmath>
#include <iostream>
#include <random>
#include <algorithm>
DaveyController::DaveyController(Assets* assets, Asset* self, ActiveAssetsManager& aam)
: assets_(assets), self_(self), aam_(aam), anim_(self, aam, true) {}

void DaveyController::update(const Input& ) {
        if (!self_ || !assets_ || !self_->info) { anim_.update(); return; }
        Asset* player = assets_->player;
        if (!player) {
                anim_.update();
                return;
        }
        if (Range::is_in_range(player, self_, 1000)) {
                constexpr int radius = 30;
                constexpr double pi = 3.14159265358979323846;
                static thread_local std::mt19937 rng{std::random_device{}()};
                if (pursue_frames_left_ <= 0) {
                        std::uniform_real_distribution<double> angle_dist(0.0, 2.0 * pi);
			double theta = angle_dist(rng);
			pursue_target_x_ = player->pos.x + static_cast<int>(std::llround(radius * std::cos(theta)));
                        pursue_target_y_ = player->pos.y + static_cast<int>(std::llround(radius * std::sin(theta)));
                        pursue_frames_left_ = pursue_recalc_interval_;
                } else {
                        pursue_frames_left_ -= 1;
                }
                int target_x = pursue_target_x_;
                int target_y = pursue_target_y_;
                anim_.set_pursue(player, 20, 30);
                anim_.move();
        } else {
        }
        anim_.update();
}
