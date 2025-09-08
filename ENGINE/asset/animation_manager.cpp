#include "animation_manager.hpp"
#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"
#include "animation.hpp"
#include "core/AssetsManager.hpp"
#include <string>
#include <iostream>
AnimationManager::AnimationManager(Asset* owner)
    : self_(owner) {}

AnimationManager::~AnimationManager() = default;

void AnimationManager::apply_pending() {
    if (!self_ || !self_->info) return;
    if (self_->next_animation.empty()) return;
    if (self_->next_animation == "end") {
        std::cout << "End called for " << self_->info->name << "\n";
        // Deleting the asset immediately here can invalidate this
        // AnimationManager mid-call. Instead, request deletion on the
        // owning asset. The Assets manager will safely purge it after
        // the update loop completes.
        self_->Delete();
        self_->next_animation.clear();
        return;
    }
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
    int dx = 0;
    int dy = 0;
    bool resort_z = false;
    bool advanced = anim.advance(
        self_->current_frame_index,
        self_->frame_progress,
        dx,
        dy,
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
    // ⬇️ This is where the block goes
    // If animation finished, trigger on_end_animation
    if (!advanced) {
        if (!anim.on_end_animation.empty()) {
            if (anim.on_end_animation == "end") {
                self_->next_animation = "end";
            } else {
                auto nit = self_->info->animations.find(anim.on_end_animation);
                if (nit != self_->info->animations.end()) {
                    self_->change_animation(anim.on_end_animation);
                }
            }
        }
    }
    apply_pending();
}

