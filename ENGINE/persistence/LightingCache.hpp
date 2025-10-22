#pragma once

#include <SDL.h>

#include <filesystem>
#include <string>

#include <nlohmann/json.hpp>

namespace world {
class Chunk;
} // namespace world

namespace lighting {

struct MeasureResult;

class LightingCache {
public:
    explicit LightingCache(std::filesystem::path root = std::filesystem::path{"loading/chunk_lighting"});

    void setRoot(std::filesystem::path root);
    bool loadManifest();

    bool saveChunk(SDL_Renderer* renderer,
                   world::Chunk& chunk,
                   SDL_Texture* mask);

    bool loadChunk(SDL_Renderer* renderer, world::Chunk& chunk);

private:
    std::filesystem::path maskPath(const world::Chunk& chunk) const;
    std::string           chunkKey(const world::Chunk& chunk) const;
    bool                  ensureDirectories() const;
    bool                  persistManifest() const;

private:
    std::filesystem::path root_{};
    std::filesystem::path manifest_path_{};
    mutable nlohmann::json manifest_{};
};

} // namespace lighting

