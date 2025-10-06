#include "AudioImporter.hpp"

namespace animation_editor {

AudioImporter::AudioImporter() = default;

void AudioImporter::set_asset_root(const std::filesystem::path& asset_root) {
    asset_root_ = asset_root;
    // TODO: Remember base path for audio copies and conversions.
}

std::filesystem::path AudioImporter::import_audio_file(const std::filesystem::path& source_path) {
    (void)source_path;
    // TODO: Copy or transcode audio into project assets and return relative path.
    return {};
}

void AudioImporter::play_preview(const std::filesystem::path& audio_path) {
    (void)audio_path;
    // TODO: Play audio clip via SDL_mixer or platform shell for preview.
}

void AudioImporter::stop_preview() {
    // TODO: Stop any currently playing preview audio clip.
}

}  // namespace animation_editor

