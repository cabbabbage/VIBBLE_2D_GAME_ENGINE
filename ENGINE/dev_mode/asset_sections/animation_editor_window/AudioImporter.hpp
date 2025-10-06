#pragma once

#include <filesystem>
#include <string>

namespace animation_editor {

class AudioImporter {
  public:
    AudioImporter();

    void set_asset_root(const std::filesystem::path& asset_root);

    std::filesystem::path import_audio_file(const std::filesystem::path& source_path);
    void play_preview(const std::filesystem::path& audio_path);
    void stop_preview();

  private:
    std::filesystem::path asset_root_;
};

}  // namespace animation_editor

