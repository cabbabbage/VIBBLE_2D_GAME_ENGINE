#include "Davey_controller.hpp"

#include "utils/input.hpp"
#include "asset/Asset.hpp"
#include "asset/determine_movement.hpp"
#include "utils/area.hpp"
#include "core/active_assets_manager.hpp"
#include "core/AssetsManager.hpp"

#include <cmath>
#include <iostream>
#include <random>

DaveyController::DaveyController(Assets* assets, Asset* self, ActiveAssetsManager& aam)
   : assets_(assets), self_(self), aam_(aam) {}

void DaveyController::update(const Input& /*input*/) {
   // Non-player follower: move towards Assets::player when within 1000px
   if (!self_ || !assets_ || !self_->info) return;

   Asset* player = assets_->player;
   if (!player) {
      // idle
      if (self_->get_current_animation() != "default") self_->change_animation("default");
      self_->update_animation_manager();
      return;
   }

   long long dx = static_cast<long long>(player->pos_X) - static_cast<long long>(self_->pos_X);
   long long dy = static_cast<long long>(player->pos_Y) - static_cast<long long>(self_->pos_Y);
   long long d2 = dx*dx + dy*dy;
   const long long r = 1000LL;
   if (d2 <= r*r) {
      std::vector<std::string> candidates = {"left", "right", "forward", "backward"};
      if (!DetermineMovement::apply_best_animation(self_, aam_, player->pos_X, player->pos_Y, candidates)) {

      }
   } else {
      if (self_->get_current_animation() != "default") self_->change_animation("default");
   }

   self_->update_animation_manager();
}
