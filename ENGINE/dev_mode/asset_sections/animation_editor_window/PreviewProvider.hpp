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
        bool flipped = false;
    };

    std::shared_ptr<SDL_Texture> build_texture(SDL_Renderer* renderer, const std::string& animation_id, int depth = 0);
    std::shared_ptr<SDL_Texture> build_texture_from_payload(SDL_Renderer* renderer, const std::string& animation_id, const nlohmann::json& payload, int depth);
    std::shared_ptr<SDL_Texture> load_folder_texture(SDL_Renderer* renderer, const std::filesystem::path& folder, int frames, bool flipped) const;
    std::vector<std::shared_ptr<SDL_Texture>> build_frame_textures(SDL_Renderer* renderer, const std::string& animation_id,
                                                                   int depth = 0);
    std::vector<FrameImageRequest> gather_frame_requests(const std::string& animation_id, int depth, bool inherited_flip);
    std::vector<FrameImageRequest> gather_frame_requests_from_payload(const std::string& animation_id,
                                                                      const nlohmann::json& payload,
                                                                      int depth,
                                                                      bool inherited_flip);
    std::filesystem::path resolve_asset_root() const;
    std::filesystem::path find_first_frame(const std::filesystem::path& folder, int frames) const;
    std::vector<std::filesystem::path> find_frame_sequence(const std::filesystem::path& folder, int frames) const;

    std::shared_ptr<AnimationDocument> document_;
    std::unordered_map<std::string, CacheEntry> cache_;
    std::unordered_map<std::string, FrameCacheEntry> frame_cache_;
    std::filesystem::path asset_root_;
};

}

