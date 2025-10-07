#include "stride_player.hpp"

#include "animation_update.hpp"

bool StridePlayer::tick(AnimationUpdate& up, Plan& plan,
                        std::size_t& stride_index, int& stride_frame_counter) {
    (void)up;
    (void)plan;
    (void)stride_index;
    (void)stride_frame_counter;
    return false;
}