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
constexpr const char* kCacheFileName = "cached_playlist.wav";
constexpr const char* kCacheMetaName = "cached_playlist.meta";

struct LoadedTrack {
    std::vector<float> samples;
    size_t frames = 0;
    int sample_rate = 0;
    int channels = 0;
    float peak = 0.0f;
    float rms = 0.0f;
    fs::path source_path;
};

std::optional<long long> file_timestamp_seconds(const fs::path& path) {
    try {
        auto ft = fs::last_write_time(path);
        auto file_now = fs::file_time_type::clock::now();
        auto sys_now = std::chrono::system_clock::now();
        auto sys_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(ft - file_now + sys_now);
        return std::chrono::duration_cast<std::chrono::seconds>(sys_time.time_since_epoch()).count();
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

bool load_wav_track(const fs::path& path, LoadedTrack& out_track) {
    SDL_AudioSpec spec;
    Uint8* buffer = nullptr;
    Uint32 length = 0;
    if (!SDL_LoadWAV(path.u8string().c_str(), &spec, &buffer, &length)) {
        std::cerr << "[AudioEngine] SDL_LoadWAV failed for '" << path.u8string() << "': " << SDL_GetError() << "\n";
        return false;
    }

    const SDL_AudioFormat desired_format = AUDIO_S16LSB;
    if (spec.format != desired_format) {
        std::cerr << "[AudioEngine] Unsupported WAV format in '" << path.u8string() << "'\n";
        SDL_FreeWAV(buffer);
        return false;
    }

    const int bytes_per_sample = SDL_AUDIO_BITSIZE(spec.format) / 8;
    if (bytes_per_sample <= 0 || spec.channels <= 0) {
        SDL_FreeWAV(buffer);
        return false;
    }

    const size_t total_samples = length / bytes_per_sample;
    const size_t frames = total_samples / static_cast<size_t>(spec.channels);
    if (frames == 0) {
        SDL_FreeWAV(buffer);
        return false;
    }

    std::vector<float> samples;
    samples.reserve(frames * spec.channels);

    const int16_t* data = reinterpret_cast<int16_t*>(buffer);
    float peak = 0.0f;
    double sum_sq = 0.0;
    const size_t total_entries = frames * static_cast<size_t>(spec.channels);
    for (size_t i = 0; i < total_entries; ++i) {
        float sample = static_cast<float>(data[i]) / 32768.0f;
        peak = std::max(peak, std::fabs(sample));
        sum_sq += static_cast<double>(sample) * static_cast<double>(sample);
        samples.push_back(sample);
    }

    SDL_FreeWAV(buffer);

    out_track.samples = std::move(samples);
    out_track.frames = frames;
    out_track.sample_rate = spec.freq;
    out_track.channels = spec.channels;
    out_track.peak = peak;
    out_track.rms = frames > 0 ? static_cast<float>(std::sqrt(sum_sq / static_cast<double>(total_entries))) : 0.0f;
    out_track.source_path = path;
    return true;
}

void apply_compressor(std::vector<float>& samples, float threshold, float ratio) {
    if (samples.empty()) return;
    if (threshold <= 0.0f || ratio <= 1.0f) return;

    for (float& sample : samples) {
        float abs_sample = std::fabs(sample);
        if (abs_sample > threshold) {
            float excess = abs_sample - threshold;
            float compressed = threshold + excess / ratio;
            sample = (sample < 0.0f ? -compressed : compressed);
        }
    }
}

void apply_delay_and_reverb(std::vector<float>& samples, int channels, int sample_rate) {
    if (samples.empty() || channels <= 0 || sample_rate <= 0) return;

    const float delay_seconds = 0.28f;
    const float reverb_seconds = 0.12f;
    const float delay_mix = 0.12f;
    const float reverb_mix = 0.08f;

    size_t delay_frames = static_cast<size_t>(delay_seconds * static_cast<float>(sample_rate));
    size_t reverb_frames = static_cast<size_t>(reverb_seconds * static_cast<float>(sample_rate));

    if (delay_frames == 0 && reverb_frames == 0) return;

    std::vector<float> processed(samples.size(), 0.0f);
    const size_t total_frames = samples.size() / static_cast<size_t>(channels);

    for (size_t frame = 0; frame < total_frames; ++frame) {
        for (int ch = 0; ch < channels; ++ch) {
            size_t idx = frame * static_cast<size_t>(channels) + static_cast<size_t>(ch);
            float dry = samples[idx];
            float value = dry * 0.88f;

            if (delay_frames > 0 && frame >= delay_frames) {
                size_t delay_idx = (frame - delay_frames) * static_cast<size_t>(channels) + static_cast<size_t>(ch);
                value += processed[delay_idx] * delay_mix;
            }
            if (reverb_frames > 0 && frame >= reverb_frames) {
                size_t reverb_idx = (frame - reverb_frames) * static_cast<size_t>(channels) + static_cast<size_t>(ch);
                value += processed[reverb_idx] * reverb_mix;
            }

            processed[idx] = value;
        }
    }

    const float wet_mix = 0.25f;
    const float dry_mix = 1.0f - wet_mix;
    for (size_t i = 0; i < samples.size(); ++i) {
        samples[i] = std::clamp(dry_mix * samples[i] + wet_mix * processed[i], -1.0f, 1.0f);
    }
}

std::vector<float> build_crossfaded_sequence(std::vector<LoadedTrack>& tracks, int sample_rate, int channels) {
    if (tracks.empty()) return {};
    std::vector<float> combined;
    const size_t fade_frames_target = static_cast<size_t>(kCrossfadeSeconds * static_cast<float>(sample_rate));

    for (size_t t = 0; t < tracks.size(); ++t) {
        LoadedTrack& track = tracks[t];
        if (track.channels != channels || track.sample_rate != sample_rate || track.frames == 0) {
            continue;
        }

        if (combined.empty()) {
            combined = track.samples;
            continue;
        }

        const size_t current_frames = combined.size() / static_cast<size_t>(channels);
        const size_t fade_frames = std::min({fade_frames_target, track.frames, current_frames});
        if (fade_frames > 0) {
            for (size_t frame = 0; frame < fade_frames; ++frame) {
                float fade_out = static_cast<float>(fade_frames - frame) / static_cast<float>(fade_frames);
                float fade_in = static_cast<float>(frame) / static_cast<float>(fade_frames);
                for (int ch = 0; ch < channels; ++ch) {
                    size_t current_idx = (current_frames - fade_frames + frame) * static_cast<size_t>(channels) + static_cast<size_t>(ch);
                    size_t next_idx = frame * static_cast<size_t>(channels) + static_cast<size_t>(ch);
                    float mixed = combined[current_idx] * fade_out + track.samples[next_idx] * fade_in;
                    combined[current_idx] = std::clamp(mixed, -1.0f, 1.0f);
                }
            }
        }

        const size_t append_offset = fade_frames * static_cast<size_t>(channels);
        const auto start_it = track.samples.begin() + static_cast<std::ptrdiff_t>(append_offset);
        combined.insert(combined.end(), start_it, track.samples.end());
    }

    if (combined.empty()) {
        return combined;
    }

    const size_t total_frames = combined.size() / static_cast<size_t>(channels);
    const size_t loop_fade_frames = std::min(fade_frames_target, total_frames);
    if (loop_fade_frames > 0) {
        std::vector<float> appended;
        appended.reserve(loop_fade_frames * static_cast<size_t>(channels));
        for (size_t frame = 0; frame < loop_fade_frames; ++frame) {
            float fade_out = static_cast<float>(loop_fade_frames - frame) / static_cast<float>(loop_fade_frames);
            float fade_in = static_cast<float>(frame) / static_cast<float>(loop_fade_frames);
            for (int ch = 0; ch < channels; ++ch) {
                size_t start_idx = frame * static_cast<size_t>(channels) + static_cast<size_t>(ch);
                size_t end_idx = (total_frames - loop_fade_frames + frame) * static_cast<size_t>(channels) + static_cast<size_t>(ch);
                float cross = combined[end_idx] * fade_out + combined[start_idx] * fade_in;
                appended.push_back(std::clamp(cross, -1.0f, 1.0f));
            }
        }
        combined.insert(combined.end(), appended.begin(), appended.end());
    }

    return combined;
}

bool write_cached_wav(const fs::path& path, const std::vector<float>& samples, int sample_rate, int channels) {
    if (samples.empty() || channels <= 0 || sample_rate <= 0) {
        return false;
    }

    std::vector<int16_t> pcm;
    pcm.reserve(samples.size());
    float peak = 0.0f;
    for (float sample : samples) {
        peak = std::max(peak, std::fabs(sample));
    }
    if (peak > 0.99f) {
        float scale = 0.99f / peak;
        for (float value : samples) {
            float scaled = std::clamp(value * scale, -1.0f, 1.0f);
            pcm.push_back(static_cast<int16_t>(std::lrint(scaled * 32767.0f)));
        }
    } else {
        for (float value : samples) {
            float clamped = std::clamp(value, -1.0f, 1.0f);
            pcm.push_back(static_cast<int16_t>(std::lrint(clamped * 32767.0f)));
        }
    }

    const uint32_t data_size = static_cast<uint32_t>(pcm.size() * sizeof(int16_t));
    const uint32_t fmt_chunk_size = 16;
    const uint32_t byte_rate = static_cast<uint32_t>(sample_rate * channels * sizeof(int16_t));
    const uint16_t block_align = static_cast<uint16_t>(channels * sizeof(int16_t));

    struct WavHeader {
        std::array<char, 4> riff;
        uint32_t chunk_size;
        std::array<char, 4> wave;
        std::array<char, 4> fmt;
        uint32_t subchunk1_size;
        uint16_t audio_format;
        uint16_t num_channels;
        uint32_t sample_rate;
        uint32_t byte_rate;
        uint16_t block_align;
        uint16_t bits_per_sample;
        std::array<char, 4> data_tag;
        uint32_t data_size;
    } header {
        {'R','I','F','F'},
        static_cast<uint32_t>(36 + data_size),
        {'W','A','V','E'},
        {'f','m','t',' '},
        fmt_chunk_size,
        1,
        static_cast<uint16_t>(channels),
        static_cast<uint32_t>(sample_rate),
        byte_rate,
        block_align,
        16,
        {'d','a','t','a'},
        data_size
    };

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }

    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out.write(reinterpret_cast<const char*>(pcm.data()), pcm.size() * sizeof(int16_t));
    return out.good();
}

bool write_cache_metadata(const fs::path& path, const std::vector<LoadedTrack>& tracks) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    out << "CACHE_V1\n";
    for (const auto& track : tracks) {
        auto timestamp = file_timestamp_seconds(track.source_path);
        if (!timestamp) continue;
        out << track.source_path.filename().u8string() << '|' << *timestamp << "\n";
    }
    return out.good();
}

bool load_cache_metadata(const fs::path& path, std::vector<std::pair<std::string, long long>>& entries) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::string header;
    std::getline(in, header);
    if (header != "CACHE_V1") {
        return false;
    }
    std::string line;
    while (std::getline(in, line)) {
        auto pos = line.find('|');
        if (pos == std::string::npos) continue;
        std::string name = line.substr(0, pos);
        long long timestamp = 0;
        try {
            timestamp = std::stoll(line.substr(pos + 1));
        } catch (const std::exception&) {
            continue;
        }
        entries.emplace_back(std::move(name), timestamp);
    }
    return true;
}

bool cache_valid(const fs::path& music_dir, const fs::path& cache_file, const fs::path& meta_file, const std::vector<fs::path>& current_files) {
    if (!fs::exists(cache_file) || !fs::exists(meta_file)) {
        return false;
    }

    std::vector<std::pair<std::string, long long>> entries;
    if (!load_cache_metadata(meta_file, entries)) {
        return false;
    }

    if (entries.empty()) {
        return false;
    }

    std::unordered_set<std::string> current_names;
    current_names.reserve(current_files.size());
    for (const auto& path : current_files) {
        current_names.insert(path.filename().u8string());
    }

    if (entries.size() != current_names.size()) {
        return false;
    }

    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& [name, timestamp] = entries[i];
        if (current_names.find(name) == current_names.end()) {
            return false;
        }
        fs::path expected = music_dir / name;
        if (!fs::exists(expected)) {
            return false;
        }
        auto current_timestamp = file_timestamp_seconds(expected);
        if (!current_timestamp || *current_timestamp != timestamp) {
            return false;
        }
    }

    return true;
}

std::optional<fs::path> build_music_cache(const fs::path& music_dir, std::vector<fs::path> files) {
    if (files.empty()) {
        return std::nullopt;
    }

    std::vector<LoadedTrack> tracks;
    tracks.reserve(files.size());

    std::mt19937 rng{std::random_device{}()};
    std::shuffle(files.begin(), files.end(), rng);

    int sample_rate = 0;
    int channels = 0;

    for (const auto& path : files) {
        LoadedTrack track;
        if (!load_wav_track(path, track)) {
            continue;
        }

        if (sample_rate == 0) {
            sample_rate = track.sample_rate;
            channels = track.channels;
        } else if (track.sample_rate != sample_rate || track.channels != channels) {
            std::cerr << "[AudioEngine] Skipping '" << path.u8string() << "' due to mismatched format\n";
            continue;
        }

        tracks.push_back(std::move(track));
    }

    if (tracks.empty() || sample_rate <= 0 || channels <= 0) {
        return std::nullopt;
    }

    std::vector<float> combined = build_crossfaded_sequence(tracks, sample_rate, channels);
    if (combined.empty()) {
        return std::nullopt;
    }

    float global_peak = 0.0f;
    float rms_accumulator = 0.0f;
    for (const auto& track : tracks) {
        global_peak = std::max(global_peak, track.peak);
        rms_accumulator += track.rms;
    }
    float average_rms = rms_accumulator / static_cast<float>(tracks.size());

    float threshold = std::clamp(average_rms * 1.4f, 0.1f, 0.85f);
    if (global_peak > 0.01f) {
        threshold = std::min(threshold, global_peak * 0.9f);
    }

    apply_compressor(combined, threshold, 3.5f);

    float post_peak = 0.0f;
    for (float sample : combined) {
        post_peak = std::max(post_peak, std::fabs(sample));
    }
    if (post_peak > 0.95f && post_peak > 0.0f) {
        float scale = 0.95f / post_peak;
        for (float& sample : combined) {
            sample *= scale;
        }
    }

    apply_delay_and_reverb(combined, channels, sample_rate);

    float final_peak = 0.0f;
    for (float sample : combined) {
        final_peak = std::max(final_peak, std::fabs(sample));
    }
    if (final_peak > 0.98f && final_peak > 0.0f) {
        float scale = 0.98f / final_peak;
        for (float& sample : combined) {
            sample *= scale;
        }
    }

    fs::path cache_file = music_dir / kCacheFileName;
    fs::path meta_file = music_dir / kCacheMetaName;

    if (!write_cached_wav(cache_file, combined, sample_rate, channels)) {
        std::cerr << "[AudioEngine] Failed to write cached playlist" << "\n";
        return std::nullopt;
    }

    if (!write_cache_metadata(meta_file, tracks)) {
        std::cerr << "[AudioEngine] Failed to write cache metadata" << "\n";
    }

    return cache_file;
}

std::optional<fs::path> prepare_music_cache(const fs::path& music_dir, const std::vector<fs::path>& files) {
    if (files.empty()) {
        return std::nullopt;
    }

    fs::path cache_file = music_dir / kCacheFileName;
    fs::path meta_file = music_dir / kCacheMetaName;

    if (cache_valid(music_dir, cache_file, meta_file, files)) {
        return cache_file;
    }

    return build_music_cache(music_dir, files);
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

void AudioEngine::init(const std::string& map_path) {
    shutdown();

    std::vector<MusicTrack> loaded;
    fs::path music_dir = fs::path(map_path) / "music";
    std::vector<fs::path> wav_files;
    try {
        if (fs::exists(music_dir) && fs::is_directory(music_dir)) {
            for (const auto& entry : fs::directory_iterator(music_dir)) {
                if (!entry.is_regular_file()) continue;
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (ext != ".wav") continue;
                wav_files.push_back(entry.path());
            }
        }
    } catch (const std::exception& ex) {
        std::cerr << "[AudioEngine] Music load error: " << ex.what() << "\n";
    }

    if (!wav_files.empty()) {
        if (auto cached = prepare_music_cache(music_dir, wav_files)) {
            Mix_Music* raw = Mix_LoadMUS(cached->u8string().c_str());
            if (raw) {
                loaded.emplace_back(raw, cached->u8string());
            } else {
                std::cerr << "[AudioEngine] Failed to load cached music '" << cached->u8string() << "': " << Mix_GetError() << "\n";
            }
        }

        if (loaded.empty()) {
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

