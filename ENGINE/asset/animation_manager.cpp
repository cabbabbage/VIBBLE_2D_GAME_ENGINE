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

void AnimationManager::update() {
    if (!self_ || !self_->info) return;

    
    if (!self_->next_animation.empty()) {
        const std::string next = self_->next_animation;

        if (next == self_->current_animation) {
            
            self_->next_animation.clear();
        } else if (next == "end") {
            
            std::cout << "End called for " << (self_->info ? self_->info->name : std::string("<unknown>")) << "\n";
            self_->next_animation.clear();
            self_->Delete();
            return;
        } else if (next == "freeze_on_last") {
            
            auto it = self_->info->animations.find(self_->current_animation);
            if (it != self_->info->animations.end()) {
                const Animation& curr = it->second;
                const int last = static_cast<int>(curr.frames.size()) - 1;
                if (self_->current_frame_index >= last) {
                    self_->static_frame = true;
                    self_->next_animation.clear();
                    return;
                }
                
            } 
        } else {
            
            auto nit = self_->info->animations.find(next);
            if (nit != self_->info->animations.end()) {
                self_->current_animation   = next;
                Animation& anim            = nit->second;
                self_->static_frame        = (static_cast<int>(anim.frames.size()) <= 1);
                self_->current_frame_index = 0;
                self_->frame_progress      = 0.0f;
            }
            self_->next_animation.clear();
        }
    }

    
    auto it = self_->info->animations.find(self_->current_animation);
    if (it == self_->info->animations.end()) return;

    Animation& anim = it->second;

    // Handle single-frame animations by triggering their on_end immediately.
    if (self_->static_frame) {
        if (self_->next_animation.empty() && !anim.on_end_animation.empty()) {
            self_->next_animation = anim.on_end_animation;
        }
        return;
    }

    int  dx = 0;
    int  dy = 0;
    bool resort_z = false;

    const bool advanced = anim.advance(self_->current_frame_index,
                                       self_->frame_progress,
                                       dx, dy, resort_z);

    self_->pos.x += dx;
    self_->pos.y += dy;

    if (!advanced && !anim.loop && self_->next_animation.empty() &&
        !anim.on_end_animation.empty()) {
        self_->next_animation = anim.on_end_animation;
    }

    if ((dx != 0 || dy != 0) && resort_z) {
        self_->set_z_index();
        if (Assets* as = self_->get_assets()) {
            as->activeManager.sortByZIndex();
        }
    }
}
