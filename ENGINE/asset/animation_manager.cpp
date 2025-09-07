#include "animation_manager.hpp"

#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"
#include "animation.hpp"
#include "core/AssetsManager.hpp" // for Assets and ActiveAssetsManager access

#include <random>
#include <string>



AnimationManager::AnimationManager(Asset* owner)
 : self_(owner)
{}

AnimationManager::~AnimationManager() = default;

void AnimationManager::apply_pending() {
 if (!self_ || !self_->info) return;
 if (self_->next_animation.empty()) return;

 if (self_->next_animation == "freeze_on_last") {
 auto it = self_->info->animations.find(self_->current_animation);
 if (it != self_->info->animations.end()) {
 const Animation& curr = it->second;
 const int last = static_cast<int>(curr.frames.size()) - 1;
 if (self_->current_frame_index == last) {
 self_->static_frame = true;
 self_->next_animation.clear();
 }
 }
 return;
 }

 auto nit = self_->info->animations.find(self_->next_animation);
 if (nit != self_->info->animations.end()) {
 self_->current_animation = self_->next_animation;
 Animation& anim = nit->second;
 self_->static_frame = (static_cast<int>(anim.frames.size()) <= 1);
 self_->current_frame_index = 0;
 self_->frame_progress = 0.0f;
 }
 self_->next_animation.clear();
}

void AnimationManager::update() {
 if (!self_ || !self_->info) return;

 apply_pending();

 auto it = self_->info->animations.find(self_->current_animation);
 if (it == self_->info->animations.end()) return;

 Animation& anim = it->second;
 if (self_->static_frame) return;

 std::string mapping_id;
 int dx = 0;
 int dy = 0;
 bool resort_z = false;

 bool advanced = anim.advance(
 self_->current_frame_index,
 self_->frame_progress,
 dx,
 dy,
 mapping_id,
 resort_z
 );

 self_->pos_X += dx;
 self_->pos_Y += dy;
 if ((dx != 0 || dy != 0) && resort_z) {
  self_->set_z_index();
  if (Assets* as = self_->get_assets()) {
   as->activeManager.sortByZIndex();
  }
 }

 if (!advanced && !mapping_id.empty()) {
 std::string resolved = self_->info->pick_next_animation(mapping_id);
 if (!resolved.empty() && self_->info->animations.count(resolved)) {
 self_->change_animation(resolved);
 } else if (self_->info->animations.count(mapping_id)) {
 self_->change_animation(mapping_id);
 }
 }

 apply_pending();
}
