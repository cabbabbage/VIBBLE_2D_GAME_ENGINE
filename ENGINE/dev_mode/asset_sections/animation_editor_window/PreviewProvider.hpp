#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

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
    void invalidate(const std::string& animation_id);
    void invalidate_all();

  private:
    struct CacheEntry {
        SDL_Renderer* renderer = nullptr;
        std::shared_ptr<SDL_Texture> texture;
        std::string signature;
};

    std::shared_ptr<SDL_Texture> build_texture(SDL_Renderer* renderer, const std::string& animation_id, int depth = 0);
    std::shared_ptr<SDL_Texture> build_texture_from_payload(SDL_Renderer* renderer, const std::string& animation_id, const nlohmann::json& payload, int depth);
    std::shared_ptr<SDL_Texture> load_folder_texture(SDL_Renderer* renderer, const std::filesystem::path& folder, int frames, bool flipped) const;
    std::filesystem::path resolve_asset_root() const;
    std::filesystem::path find_first_frame(const std::filesystem::path& folder, int frames) const;

    std::shared_ptr<AnimationDocument> document_;
    std::unordered_map<std::string, CacheEntry> cache_;
    std::filesystem::path asset_root_;
};

}

