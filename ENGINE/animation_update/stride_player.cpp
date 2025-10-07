#include "stride_player.hpp"

#include "animation_update.hpp"
#include "asset/Asset.hpp"
#include "asset/animation.hpp"
#include "asset/animation_frame.hpp"

namespace {
constexpr const char* kDefaultAnimation = "default";
}

bool StridePlayer::tick(AnimationUpdate& up, Plan& plan,
                        std::size_t& stride_index, int& stride_frame_counter) {
    Asset* self = up.self_;
    if (!self || !self->info) {
        return false;
    }

    if (plan.strides.empty() || stride_index >= plan.strides.size()) {
        plan.strides.clear();
        stride_index         = 0;
        stride_frame_counter = 0;
        return false;
    }

    auto abort_plan = [&]() {
        plan.strides.clear();
        plan.sanitized_checkpoints.clear();
        plan.final_dest = self->pos;
        stride_index    = 0;
        stride_frame_counter = 0;
        up.switch_to(kDefaultAnimation);
        up.path_requested = true;
    };

    Stride& stride = plan.strides[stride_index];
    if (stride.frames <= 0) {
        ++stride_index;
        stride_frame_counter = 0;
        if (stride_index >= plan.strides.size()) {
            plan.strides.clear();
            return false;
        }
        stride = plan.strides[stride_index];
    }

    if (self->current_animation != stride.animation_id) {
        up.switch_to(stride.animation_id);
        stride_frame_counter = 0;
    }

    if (stride_index == plan.strides.size() - 1 && stride_frame_counter == 0) {
        up.path_requested = true;
    }

    auto anim_it = self->info->animations.find(self->current_animation);
    if (anim_it == self->info->animations.end()) {
        abort_plan();
        return false;
    }

    Animation& anim = anim_it->second;
    if (!self->current_frame) {
        self->current_frame = anim.get_first_frame();
        if (!self->current_frame) {
            abort_plan();
            return false;
        }
    }

    AnimationFrame* frame = self->current_frame;
    SDL_Point from = self->pos;
    SDL_Point delta{ frame->dx, frame->dy };
    SDL_Point to{ from.x + delta.x, from.y + delta.y };

    if ((delta.x != 0 || delta.y != 0) && up.path_blocked(from, to, self)) {
        abort_plan();
        return false;
    }

    if (delta.x != 0 || delta.y != 0) {
        self->pos = to;
        self->set_z_index();
    }

    ++stride_frame_counter;
    bool stride_complete = stride_frame_counter >= stride.frames;

    if (!stride_complete) {
        if (!up.advance(self->current_frame)) {
            stride_complete = true;
            self->current_frame = anim.get_first_frame();
        }
    } else {
        if (!up.advance(self->current_frame)) {
            self->current_frame = anim.get_first_frame();
        }
    }

    if (stride_complete) {
        ++stride_index;
        stride_frame_counter = 0;
        if (stride_index >= plan.strides.size()) {
            plan.strides.clear();
            return false;
        }
        const Stride& next_stride = plan.strides[stride_index];
        up.switch_to(next_stride.animation_id);
        if (stride_index == plan.strides.size() - 1) {
            up.path_requested = true;
        }
    }

    return true;
}
