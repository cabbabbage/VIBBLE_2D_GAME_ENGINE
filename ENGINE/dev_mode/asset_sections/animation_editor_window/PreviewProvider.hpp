#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json_fwd.hpp>

struct SDL_Renderer;
struct SDL_Texture;

namespace animation_editor {

class AnimationDocument;

class PreviewProvider {
  public:
    PreviewProvider();

    void set_document(std::shared_ptr<AnimationDocument> document);

    SDL_Texture* get_preview_texture(SDL_Renderer* renderer, const std::string& animation_id);
    SDL_Texture* get_frame_texture(SDL_Renderer* renderer, const std::string& animation_id, int frame_index);
    void invalidate(const std::string& animation_id);
    void invalidate_all();

    int get_frame_count(const std::string& animation_id) const;

  private:
    struct CacheEntry {
        SDL_Renderer* renderer = nullptr;
        std::shared_ptr<SDL_Texture> texture;
        std::string signature;
};

    struct FrameCacheEntry {
        SDL_Renderer* renderer = nullptr;
        std::string signature;
        std::vector<std::shared_ptr<SDL_Texture>> textures;
};

    struct FrameImageRequest {
        std::filesystem::path path;
        bool flip_x = false;
        bool flip_y = false;
};

    struct ResolvedAnimation {
        std::vector<FrameImageRequest> frames;
        std::string signature;
};

    std::shared_ptr<SDL_Texture> build_texture(SDL_Renderer* renderer, const std::string& animation_id, int depth = 0);
    std::shared_ptr<SDL_Texture> build_texture_from_resolved(SDL_Renderer* renderer, const ResolvedAnimation& resolved);
    std::vector<std::shared_ptr<SDL_Texture>> build_frame_textures(SDL_Renderer* renderer, const ResolvedAnimation& resolved);
    ResolvedAnimation resolve_animation(const std::string& animation_id, int depth = 0) const;
    std::filesystem::path resolve_asset_root() const;
    std::filesystem::path find_first_frame(const std::filesystem::path& folder, int frames) const;
    std::vector<std::filesystem::path> find_frame_sequence(const std::filesystem::path& folder, int frames) const;

    std::shared_ptr<AnimationDocument> document_;
    std::unordered_map<std::string, CacheEntry> cache_;
    std::unordered_map<std::string, FrameCacheEntry> frame_cache_;
    std::filesystem::path asset_root_;
};

}
