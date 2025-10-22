#pragma once

#include <filesystem>
#include <memory>
#include <string>

struct Mix_Chunk;

namespace animation_editor {

class AudioImporter {
  public:
    AudioImporter();

    void set_asset_root(const std::filesystem::path& asset_root);

    std::filesystem::path import_audio_file(const std::filesystem::path& source_path);
    void play_preview(const std::filesystem::path& audio_path);
    void stop_preview();
    bool is_previewing() const;
    std::filesystem::path resolve_asset_path(const std::filesystem::path& relative) const;

  private:
    std::filesystem::path asset_root_;
    mutable std::shared_ptr<::Mix_Chunk> preview_chunk_;
    mutable int preview_channel_ = -1;
};

}

