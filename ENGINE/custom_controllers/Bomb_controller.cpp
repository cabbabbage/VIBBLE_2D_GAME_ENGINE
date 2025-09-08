#include "Bomb_controller.hpp"

#include "utils/input.hpp"
#include "asset/Asset.hpp"
#include "asset/auto_movement.hpp"
#include "core/active_assets_manager.hpp"
#include "core/AssetsManager.hpp"

#include <algorithm>



BombController::BombController(Assets* assets, Asset* self, ActiveAssetsManager& aam)
 : assets_(assets)
 , self_(self)
 , aam_(aam)
 , mover_(self, aam, true)
{
  rng_seed_ ^= reinterpret_cast<uintptr_t>(self_) + 0x9e3779b9u;
}

BombController::~BombController() {}

void BombController::update(const Input& /*in*/) {
  updated_by_determine_ = false;
  if (!self_ || !self_->info) return;

  Asset* player = assets_ ? assets_->player : nullptr;
  explosion_if_close(player);

  if (self_->get_current_animation() == "explosion") {
    self_->update_animation_manager();
    return;
  }

  if (player && self_->distance_to_player <= static_cast<float>(follow_radius_))
    pursue(player);
  else
    think_random();

  if (!updated_by_determine_) self_->update_animation_manager();
}

void BombController::think_random() {
  if (!self_) return;

  
  mover_.set_idle(/*min=*/0, /*max=*/probe_, /*rest_ratio=*/3);
  mover_.move();
  updated_by_determine_ = true;
}

void BombController::pursue(Asset* player) {
  if (!self_ || !player) return;

  
  mover_.set_pursue(/*final_target=*/player, /*min=*/20, /*max=*/30);
  mover_.move();
  updated_by_determine_ = true;
}



void BombController::explosion_if_close(Asset* player) {
  if (!self_ || !player) return;
  
  if (self_->get_current_animation() == "explosion") {
    if (self_->is_current_animation_last_frame() && !self_->is_current_animation_looping()) {
      self_->Delete();
    }
    return;
  }

  
  float d = self_->distance_to_player;
  if (d <= static_cast<float>(explosion_radius_)) {
    if (self_->get_current_animation() != "explosion")
      self_->change_animation("explosion");
  }
}

