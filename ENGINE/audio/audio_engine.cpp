#include "audio/audio_engine.hpp"

#include "asset/Asset.hpp"
#include "asset/animation.hpp"

#include <SDL_mixer.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {
AudioEngine* g_active_audio_engine = nullptr;
}

AudioEngine& AudioEngine::instance() {
    static AudioEngine engine;
    return engine;
}

AudioEngine::MusicTrack::MusicTrack()
    : music(nullptr, Mix_FreeMusic) {}

AudioEngine::MusicTrack::MusicTrack(Mix_Music* raw, std::string path)
    : music(raw, Mix_FreeMusic), file_path(std::move(path)) {}

AudioEngine::MusicTrack::MusicTrack(MusicTrack&& other) noexcept = default;

AudioEngine::MusicTrack& AudioEngine::MusicTrack::operator=(MusicTrack&& other) noexcept = default;

void AudioEngine::init(const std::string& map_path) {
    shutdown();

    std::vector<MusicTrack> loaded;
    fs::path music_dir = fs::path(map_path) / "music";
    try {
        if (fs::exists(music_dir) && fs::is_directory(music_dir)) {
            for (const auto& entry : fs::directory_iterator(music_dir)) {
                if (!entry.is_regular_file()) continue;
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (ext != ".wav") continue;
                std::string abs_path = entry.path().u8string();
                Mix_Music* raw = Mix_LoadMUS(abs_path.c_str());
                if (!raw) {
                    std::cerr << "[AudioEngine] Failed to load music '" << abs_path << "': " << Mix_GetError() << "\n";
                    continue;
                }
                loaded.emplace_back(raw, abs_path);
            }
        }
    } catch (const std::exception& ex) {
        std::cerr << "[AudioEngine] Music load error: " << ex.what() << "\n";
    }

    if (!loaded.empty()) {
        std::mt19937 rng{std::random_device{}()};
        std::shuffle(loaded.begin(), loaded.end(), rng);
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        playlist_ = std::move(loaded);
        current_map_ = map_path;
        next_track_index_ = 0;
        playlist_started_ = false;
    }

    pending_next_track_.store(!playlist_.empty(), std::memory_order_relaxed);

    if (!playlist_.empty()) {
        g_active_audio_engine = this;
        Mix_AllocateChannels(64);
        Mix_HookMusicFinished(&AudioEngine::music_finished_callback);
        Mix_VolumeMusic(static_cast<int>(MIX_MAX_VOLUME * 0.6));
        update();
    } else {
        g_active_audio_engine = nullptr;
        Mix_HookMusicFinished(nullptr);
    }
}

void AudioEngine::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!playlist_.empty() || playlist_started_) {
            Mix_HaltMusic();
        }
        playlist_.clear();
        current_map_.clear();
        next_track_index_ = 0;
        playlist_started_ = false;
    }
    pending_next_track_.store(false, std::memory_order_relaxed);
    Mix_HookMusicFinished(nullptr);
    g_active_audio_engine = nullptr;
}

void AudioEngine::play_next_track_locked() {
    if (playlist_.empty()) {
        playlist_started_ = false;
        return;
    }

    const size_t total = playlist_.size();
    for (size_t attempt = 0; attempt < total; ++attempt) {
        size_t index = next_track_index_;
        next_track_index_ = (next_track_index_ + 1) % total;
        MusicTrack& track = playlist_[index];
        if (!track.valid()) {
            continue;
        }
        if (Mix_PlayMusic(track.music.get(), 1) == -1) {
            std::cerr << "[AudioEngine] Mix_PlayMusic failed for '" << track.file_path << "': " << Mix_GetError() << "\n";
            continue;
        }
        playlist_started_ = true;
        return;
    }
    playlist_started_ = false;
}

void AudioEngine::handle_music_finished() {
    pending_next_track_.store(true, std::memory_order_relaxed);
}

void AudioEngine::music_finished_callback() {
    if (g_active_audio_engine) {
        g_active_audio_engine->handle_music_finished();
    }
}

void AudioEngine::update() {
    if (pending_next_track_.exchange(false, std::memory_order_relaxed)) {
        std::lock_guard<std::mutex> lock(mutex_);
        play_next_track_locked();
        return;
    }

    if (!Mix_PlayingMusic()) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (playlist_started_) {
            play_next_track_locked();
        }
    }
}

void AudioEngine::set_effect_max_distance(float distance) {
    if (!std::isfinite(distance) || distance <= 0.0f) {
        distance = 1.0f;
    }
    effect_max_distance_.store(distance, std::memory_order_relaxed);
}

void AudioEngine::play_now(const Animation& animation, const Asset& asset) {
    const Animation::AudioClip* clip = animation.audio_data();
    if (!clip || !clip->chunk) {
        return;
    }

    Mix_Chunk* chunk = clip->chunk.get();
    if (!chunk) {
        return;
    }

    float max_distance = effect_max_distance_.load(std::memory_order_relaxed);
    if (max_distance < 1.0f) {
        max_distance = 1.0f;
    }

    float distance = asset.distance_from_camera;
    if (!std::isfinite(distance) || distance < 0.0f) {
        distance = 0.0f;
    }

    float normalized = distance / max_distance;
    if (normalized > 1.0f) normalized = 1.0f;
    if (normalized < 0.0f) normalized = 0.0f;

    const float base_volume = static_cast<float>(clip->volume) / 100.0f;
    float distance_scale = 1.0f - normalized;
    distance_scale = distance_scale * distance_scale;
    float final_volume = base_volume * distance_scale;
    if (final_volume <= 0.0f) {
        return;
    }

    int channel = Mix_PlayChannel(-1, chunk, 0);
    if (channel == -1) {
        std::cerr << "[AudioEngine] Mix_PlayChannel failed: " << Mix_GetError() << "\n";
        return;
    }

    int sdl_volume = static_cast<int>(std::lround(final_volume * MIX_MAX_VOLUME));
    sdl_volume = std::clamp(sdl_volume, 0, MIX_MAX_VOLUME);
    Mix_Volume(channel, sdl_volume);

    float pan_basis = std::cos(asset.angle_from_camera);
    if (!std::isfinite(pan_basis)) {
        pan_basis = 0.0f;
    }
    pan_basis = std::clamp(pan_basis, -1.0f, 1.0f);

    float left_mix = 0.5f * (1.0f - pan_basis);
    float right_mix = 0.5f * (1.0f + pan_basis);

    const float crossfeed = 0.2f;
    left_mix = left_mix * (1.0f - crossfeed) + crossfeed;
    right_mix = right_mix * (1.0f - crossfeed) + crossfeed;

    left_mix = std::clamp(left_mix, 0.0f, 1.0f);
    right_mix = std::clamp(right_mix, 0.0f, 1.0f);

    Uint8 left = static_cast<Uint8>(std::lround(left_mix * 255.0f));
    Uint8 right = static_cast<Uint8>(std::lround(right_mix * 255.0f));
    if (left == 0 && right == 0) {
        left = right = 1;
    }

    if (Mix_SetPanning(channel, left, right) == 0) {
        std::cerr << "[AudioEngine] Mix_SetPanning failed: " << Mix_GetError() << "\n";
    }
}

