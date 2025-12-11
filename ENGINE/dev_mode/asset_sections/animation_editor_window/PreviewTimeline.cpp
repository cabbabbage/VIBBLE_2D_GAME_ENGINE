#include <algorithm>

#include "PreviewTimeline.hpp"

namespace animation_editor {

PreviewTimeline::PreviewTimeline()
    : start_ticks_(SDL_GetTicks())
{
}

void PreviewTimeline::play() {
    if (playing_) return;
    playing_ = true;
    if (start_ticks_ == 0) {
        start_ticks_ = SDL_GetTicks();
    }
}

void PreviewTimeline::pause() {
    playing_ = false;
}

void PreviewTimeline::stop() {
    playing_ = false;
    start_ticks_ = 0;
    scrub_frame_ = 0;
    scrubbing_ = false;
}

void PreviewTimeline::set_current_frame(int frame) {
    scrub_frame_ = std::max(0, std::min(frame_count_ - 1, frame));
    scrubbing_ = true;
    start_ticks_ = SDL_GetTicks() - static_cast<Uint32>((scrub_frame_ * 1000.0f) / fps_);
}

int PreviewTimeline::current_frame() const {
    if (scrubbing_) {
        return scrub_frame_;
    }
    if (!playing_ || start_ticks_ == 0) {
        return scrub_frame_;
    }
    Uint32 elapsed = SDL_GetTicks() - start_ticks_;
    float total_frames_float = elapsed / 1000.0f * fps_;
    int total_frames = static_cast<int>(total_frames_float);
    if (!loop_) {
        return std::min(total_frames, frame_count_ - 1);
    }
    return total_frames % frame_count_;
}

bool PreviewTimeline::update() {
    int previous_frame = current_frame();
    if (scrubbing_) {
        scrubbing_ = false;
    }
    return current_frame() != previous_frame;
}

}
