#pragma once

#include <cstdint>

#include <SDL.h>

namespace animation_editor {

class PreviewTimeline {
public:
    PreviewTimeline();

    void set_fps(float fps) { fps_ = fps; if (fps_ < 0.01f) fps_ = 0.01f; }
    float fps() const { return fps_; }

    void set_loop(bool loop) { loop_ = loop; }
    bool loop() const { return loop_; }

    void set_frame_count(int count) { frame_count_ = count; if (frame_count_ < 1) frame_count_ = 1; }
    int frame_count() const { return frame_count_; }

    void play();
    void pause();
    void stop();
    bool is_playing() const { return playing_; }

    void set_current_frame(int frame);
    int current_frame() const;

    bool update();

private:
    bool playing_ = true;
    float fps_ = 24.0f;
    bool loop_ = true;
    int frame_count_ = 1;
    Uint32 start_ticks_ = 0;
    int scrub_frame_ = 0;
    bool scrubbing_ = false;
};

}
