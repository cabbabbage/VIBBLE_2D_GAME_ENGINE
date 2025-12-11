#include "movement_plan_executor.hpp"

#include <vector>

#include "animation_runtime.hpp"
#include "asset/Asset.hpp"
#include "asset/animation.hpp"
#include "asset/animation_frame.hpp"
#include "animation_update.hpp"

bool MovementPlanExecutor::tick(AnimationRuntime& up, Plan& plan,
                        std::size_t& stride_index, int& stride_frame_counter) {
    Asset* self = up.self_;
    if (!self || !self->info) {
        return false;
    }

    auto check_target_reached = [&]() {
        if (self && up.planner_iface_) {
            const int visited_thresh = up.planner_iface_->visit_threshold_px();
            const int visited_thresh_squared = visited_thresh * visited_thresh;
            const int dist_sq = (self->pos.x - plan.final_dest.x) * (self->pos.x - plan.final_dest.x) + (self->pos.y - plan.final_dest.y) * (self->pos.y - plan.final_dest.y);
            if (dist_sq <= visited_thresh_squared) {
                self->target_reached = true;
            }
        }
};

    if (plan.strides.empty() || stride_index >= plan.strides.size()) {
        check_target_reached();
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
        up.switch_to(animation_update::detail::kDefaultAnimation, 0);
        if (self) { self->needs_target = true; }
};

    Stride& stride = plan.strides[stride_index];
    if (stride.frames <= 0) {
        ++stride_index;
        stride_frame_counter = 0;
        if (stride_index >= plan.strides.size()) {
            check_target_reached();
            plan.strides.clear();
            return false;
        }
        stride = plan.strides[stride_index];
    }

    const std::size_t stride_path = stride.path_index;
    const bool same_animation     = (self->current_animation == stride.animation_id);
    const bool same_path          = same_animation && (up.path_index_for(stride.animation_id) == stride_path);
    if (!same_animation || !same_path) {
        up.switch_to(stride.animation_id, stride_path);
        stride_frame_counter = 0;
    }

    if (stride_index == plan.strides.size() - 1 && stride_frame_counter == 0) {
        if (self) { self->needs_target = true; }
    }

    auto anim_it = self->info->animations.find(self->current_animation);
    if (anim_it == self->info->animations.end()) {
        abort_plan();
        return false;
    }

    Animation& anim = anim_it->second;
    const std::size_t current_path = up.path_index_for(self->current_animation);
    if (!self->current_frame) {
        self->current_frame = anim.get_first_frame(current_path);
        if (!self->current_frame) {
            abort_plan();
            return false;
        }
    }

    AnimationFrame* frame = self->current_frame;
    SDL_Point        from  = self->pos;
    SDL_Point        delta{0, 0};
    if (!up.suppress_root_motion_active()) {
        delta = animation_update::detail::frame_world_delta(*frame, *self, up.grid());
    }
    SDL_Point        to{ from.x + delta.x, from.y + delta.y };

    if (delta.x != 0 || delta.y != 0) {
        std::vector<const Asset*> blockers;
        if (up.path_blocked(from, to, self, &blockers)) {
            if (up.handle_blocked_path(from, to, blockers)) {
                return true;
            }
            abort_plan();
            return false;
        }
    }

    if (delta.x != 0 || delta.y != 0) {
        self->pos = to;
        if (!frame || frame->z_resort) {
            up.refresh_z_index();
        }
        up.mark_progress_toward_checkpoints();
    }

    ++stride_frame_counter;
    bool stride_complete = stride_frame_counter >= stride.frames;

    if (!stride_complete) {
        if (!up.advance(self->current_frame)) {
            stride_complete = true;
            self->current_frame = anim.get_first_frame(current_path);
        }
    } else {
        if (!up.advance(self->current_frame)) {
            self->current_frame = anim.get_first_frame(current_path);
        }
    }

    if (stride_complete) {
        ++stride_index;
        stride_frame_counter = 0;
        if (stride_index >= plan.strides.size()) {
            check_target_reached();
            plan.strides.clear();
            return false;
        }
        const Stride& next_stride = plan.strides[stride_index];
        up.switch_to(next_stride.animation_id, next_stride.path_index);
        if (stride_index == plan.strides.size() - 1) {
            if (self) { self->needs_target = true; }
        }
    }

    return true;
}
