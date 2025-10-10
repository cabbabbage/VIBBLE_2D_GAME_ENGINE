#include "AudioImporter.hpp"

#include <SDL_log.h>
#include <SDL_mixer.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string_view>
#include <system_error>

namespace animation_editor {

namespace {

std::string to_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool has_extension(const std::filesystem::path& path, std::string_view ext) {
    return to_lower(path.extension().string()) == to_lower(std::string(ext));
}

std::string quote(const std::filesystem::path& path) {
    std::string text = path.string();
    std::string quoted;
    quoted.reserve(text.size() + 2);
    quoted.push_back('"');
    for (char ch : text) {
        if (ch == '"') quoted.push_back('\\');
        quoted.push_back(ch);
    }
    quoted.push_back('"');
    return quoted;
}

void ensure_directory(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec && !std::filesystem::exists(path)) {
        throw std::system_error(ec, "Failed to create directory");
    }
}

std::filesystem::path normalize_destination(const std::filesystem::path& asset_root,
                                            const std::filesystem::path& source_path) {
    std::filesystem::path stem = source_path.stem();
    if (stem.empty()) {
        stem = "clip";
    }
    std::filesystem::path dest = asset_root / (stem.string() + ".wav");
    return dest.lexically_normal();
}

}

AudioImporter::AudioImporter() = default;

void AudioImporter::set_asset_root(const std::filesystem::path& asset_root) {
    asset_root_ = asset_root;
}

std::filesystem::path AudioImporter::import_audio_file(const std::filesystem::path& source_path) {
    if (asset_root_.empty()) {
        SDL_Log("AudioImporter: asset root not configured");
        return {};
    }
    if (source_path.empty() || !std::filesystem::exists(source_path)) {
        SDL_Log("AudioImporter: source '%s' does not exist", source_path.string().c_str());
        return {};
    }

    try {
        ensure_directory(asset_root_);
    } catch (const std::exception& ex) {
        SDL_Log("AudioImporter: failed to prepare asset directory: %s", ex.what());
        return {};
    }

    std::filesystem::path destination = normalize_destination(asset_root_, source_path);

    const bool is_wav = has_extension(source_path, ".wav");
    bool converted = false;

    if (!is_wav) {
        std::string command = "ffmpeg -y -i " + quote(source_path) + " " + quote(destination);
        int rc = std::system(command.c_str());
        if (rc == 0) {
            converted = true;
        }
    }

    if (!converted) {
        std::error_code ec;
        std::filesystem::copy_file(source_path, destination, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            SDL_Log("AudioImporter: failed to copy audio '%s': %s", source_path.string().c_str(), ec.message().c_str());
            return {};
        }
    }

    return destination;
}

void AudioImporter::play_preview(const std::filesystem::path& audio_path) {
    stop_preview();

    std::filesystem::path absolute = audio_path;
    if (!absolute.is_absolute() && !asset_root_.empty()) {
        absolute = asset_root_ / audio_path;
    }
    absolute = absolute.lexically_normal();

    if (!std::filesystem::exists(absolute)) {
        SDL_Log("AudioImporter: preview file missing '%s'", absolute.string().c_str());
        return;
    }

    Mix_Chunk* raw = Mix_LoadWAV(absolute.string().c_str());
    if (!raw) {
        SDL_Log("AudioImporter: failed to load preview '%s': %s", absolute.string().c_str(), Mix_GetError());
        return;
    }

    preview_chunk_.reset(raw, Mix_FreeChunk);
    preview_channel_ = Mix_PlayChannel(-1, preview_chunk_.get(), 0);
    if (preview_channel_ < 0) {
        SDL_Log("AudioImporter: failed to play preview: %s", Mix_GetError());
        preview_chunk_.reset();
    }
}

void AudioImporter::stop_preview() {
    if (preview_channel_ >= 0) {
        Mix_HaltChannel(preview_channel_);
        preview_channel_ = -1;
    }
    preview_chunk_.reset();
}

bool AudioImporter::is_previewing() const {
    if (preview_channel_ < 0) {
        return false;
    }
    if (Mix_Playing(preview_channel_) == 0) {
        preview_channel_ = -1;
        preview_chunk_.reset();
        return false;
    }
    return true;
}

std::filesystem::path AudioImporter::resolve_asset_path(const std::filesystem::path& relative) const {
    if (relative.empty()) return {};
    if (relative.is_absolute()) return relative;
    if (asset_root_.empty()) return relative;
    return (asset_root_ / relative).lexically_normal();
}

}

