#pragma once

#include <SDL_mixer.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class Animation;
class Asset;

class AudioEngine {
public:
    static AudioEngine& instance();

    void init(const std::string& map_path);
    void shutdown();
    void update();

    void set_effect_max_distance(float distance);

    void play_now(const Animation& animation, const Asset& asset);

private:
    AudioEngine() = default;
    ~AudioEngine() = default;
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    struct MusicTrack {
        using MusicPtr = std::unique_ptr<Mix_Music, void(*)(Mix_Music*)>;
        MusicTrack();
        MusicTrack(Mix_Music* music, std::string path);
        MusicTrack(MusicTrack&& other) noexcept;
        MusicTrack& operator=(MusicTrack&& other) noexcept;
        MusicTrack(const MusicTrack&) = delete;
        MusicTrack& operator=(const MusicTrack&) = delete;
        bool valid() const { return static_cast<bool>(music); }

        MusicPtr music;
        std::string file_path;
};

    void play_next_track_locked();
    void handle_music_finished();
    static void music_finished_callback();

    std::mutex mutex_;
    std::vector<MusicTrack> playlist_;
    std::string current_map_;
    std::atomic<bool> pending_next_track_{false};
    std::atomic<float> effect_max_distance_{1200.0f};
    size_t next_track_index_ = 0;
    bool playlist_started_ = false;
};

