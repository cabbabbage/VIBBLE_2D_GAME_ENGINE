#include "get_best_path.hpp"

#include "asset/Asset.hpp"
#include "asset/animation.hpp"

Plan GetBestPath::operator()(const Asset& self,
                             const std::vector<SDL_Point>& sanitized_checkpoints,
                             int) const {
    Plan plan;
    plan.sanitized_checkpoints = sanitized_checkpoints;
    if (!sanitized_checkpoints.empty()) {
        plan.final_dest = sanitized_checkpoints.back();
    } else {
        plan.final_dest = self.pos;
    }
    return plan;
}
