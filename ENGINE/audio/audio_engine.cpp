#include "audio/audio_engine.hpp"

#include "asset/Asset.hpp"
#include "asset/animation.hpp"

#include <SDL.h>
#include <SDL_mixer.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <optional>
#include <random>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {
AudioEngine* g_active_audio_engine = nullptr;

constexpr float kCrossfadeSeconds = 5.0f;

struct LoadedTrack {
    std::vector<float> samples;
    size_t frames = 0;
    int sample_rate = 0;
    int channels = 0;
    float peak = 0.0f;
    float rms = 0.0f;
    fs::path source_path;
};

fs::path resolve_with_base(const fs::path& candidate, const fs::path& base_root) {
    if (candidate.empty()) {
        return {};
    }
    if (candidate.is_absolute()) {
        return candidate;
    }
    if (!base_root.empty()) {
        return base_root / candidate;
    }
    return candidate;
}

std::vector<fs::path> collect_music_files(const nlohmann::json& audio_manifest,
                                          const std::string& content_root_hint) {
    std::vector<fs::path> result;
    if (!audio_manifest.is_object()) {
        return result;
    }

    const auto music_it = audio_manifest.find("music");
    if (music_it == audio_manifest.end() || !music_it->is_object()) {
        return result;
    }

    const nlohmann::json& music = *music_it;
    fs::path fallback_root = content_root_hint.empty() ? fs::path{} : fs::path(content_root_hint);
    fs::path base_root = fallback_root;
    if (auto root_it = music.find("content_root"); root_it != music.end() && root_it->is_string()) {
        fs::path declared = root_it->get<std::string>();
        if (!declared.is_absolute()) {
            declared = resolve_with_base(declared, fallback_root);
        }
        base_root = declared;
    }

    auto tracks_it = music.find("tracks");
    if (tracks_it == music.end() || !tracks_it->is_array()) {
        return result;
    }

    for (const auto& entry : *tracks_it) {
        fs::path local_base = base_root;
        fs::path track_path;
        if (entry.is_string()) {
            track_path = fs::path(entry.get<std::string>());
        } else if (entry.is_object()) {
            if (auto local_root_it = entry.find("content_root");
                local_root_it != entry.end() && local_root_it->is_string()) {
                fs::path declared = local_root_it->get<std::string>();
                if (!declared.is_absolute()) {
                    declared = resolve_with_base(declared, base_root.empty() ? fallback_root : base_root);
                }
                local_base = declared;
            }

            std::string path_value;
            if (auto path_it = entry.find("path"); path_it != entry.end() && path_it->is_string()) {
                path_value = path_it->get<std::string>();
            } else if (auto file_it = entry.find("file"); file_it != entry.end() && file_it->is_string()) {
                path_value = file_it->get<std::string>();
            }

            if (!path_value.empty()) {
                track_path = fs::path(path_value);
            }
        }

        if (track_path.empty()) {
            continue;
        }

        fs::path resolved = resolve_with_base(track_path, local_base);
        if (resolved.empty()) {
            continue;
        }

        try {
            resolved = fs::absolute(resolved);
        } catch (...) {
        }

        result.push_back(resolved);
    }

    return result;
}

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

void AudioEngine::init(const std::string& map_id,
                       const nlohmann::json& audio_manifest,
                       const std::string& content_root_hint) {
    shutdown();

    std::vector<MusicTrack> loaded;
    std::vector<fs::path> wav_files = collect_music_files(audio_manifest, content_root_hint);

    for (auto it = wav_files.begin(); it != wav_files.end();) {
        try {
            if (!fs::exists(*it)) {
                std::cerr << "[AudioEngine] Music track not found: " << it->u8string() << "\n";
                it = wav_files.erase(it);
                continue;
            }
        } catch (const std::exception& ex) {
            std::cerr << "[AudioEngine] Music path check failed for '" << it->u8string() << "': " << ex.what() << "\n";
            it = wav_files.erase(it);
            continue;
        }
        ++it;
    }

    if (!wav_files.empty()) {
        for (const auto& path : wav_files) {
            std::string abs_path = path.u8string();
            Mix_Music* raw = Mix_LoadMUS(abs_path.c_str());
            if (!raw) {
                std::cerr << "[AudioEngine] Failed to load music '" << abs_path << "': " << Mix_GetError() << "\n";
                continue;
            }
            loaded.emplace_back(raw, abs_path);
        }
        if (loaded.size() > 1) {
            std::mt19937 rng{std::random_device{}()};
            std::shuffle(loaded.begin(), loaded.end(), rng);
        }
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        playlist_ = std::move(loaded);
        current_map_ = map_id;
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
        int loops = (playlist_.size() == 1) ? -1 : 1;
        int fade_ms = static_cast<int>(kCrossfadeSeconds * 1000.0f);
        if (Mix_FadeInMusic(track.music.get(), loops, fade_ms) == -1) {
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

