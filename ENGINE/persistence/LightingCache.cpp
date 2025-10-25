#include "persistence/LightingCache.hpp"

#include "lighting/PreloadInputs.hpp"
#include "utils/RenderReadback.hpp"
#include "utils/log.hpp"
#include "world/chunk.hpp"

#include <algorithm>
#include <exception>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace lighting {

LightingCache::LightingCache(std::filesystem::path root)
    : root_(std::move(root)) {
    manifest_path_ = root_ / "manifest.json";
    loadManifest();
}

void LightingCache::setRoot(std::filesystem::path root) {
    root_          = std::move(root);
    manifest_path_ = root_ / "manifest.json";
    loadManifest();
}

bool LightingCache::ensureDirectories() const {
    std::error_code ec;
    if (!std::filesystem::exists(root_)) {
        if (!std::filesystem::create_directories(root_, ec) && ec) {
            vibble::log::warn(std::string{"[Lighting] Failed to create lighting cache directory: "} + ec.message());
            return false;
        }
    }
    return true;
}

bool LightingCache::persistManifest() const {
    if (!ensureDirectories()) {
        return false;
    }
    std::ofstream file(manifest_path_, std::ios::binary | std::ios::trunc);
    if (!file) {
        vibble::log::warn("[Lighting] Unable to open manifest for writing");
        return false;
    }
    file << manifest_.dump(2);
    return true;
}

bool LightingCache::loadManifest() {
    manifest_.clear();
    if (!std::filesystem::exists(manifest_path_)) {
        return true;
    }

    std::ifstream file(manifest_path_, std::ios::binary);
    if (!file) {
        return false;
    }

    try {
        file >> manifest_;
    } catch (const std::exception& ex) {
        vibble::log::warn(std::string{"[Lighting] Failed to parse lighting manifest: "} + ex.what());
        manifest_ = nlohmann::json::object();
        return false;
    }

    if (!manifest_.is_object()) {
        manifest_ = nlohmann::json::object();
    }
    return true;
}

std::string LightingCache::chunkKey(const world::Chunk& chunk) const {
    return std::to_string(chunk.i) + "_" + std::to_string(chunk.j);
}

std::filesystem::path LightingCache::maskPath(const world::Chunk& chunk) const {
    return root_ / (chunkKey(chunk) + ".rgba");
}

bool LightingCache::saveChunk(SDL_Renderer* renderer,
                              world::Chunk& chunk,
                              SDL_Texture* mask) {
    (void)renderer;
    (void)chunk;
    (void)mask;
    return false;
}

bool LightingCache::loadChunk(SDL_Renderer* renderer, world::Chunk& chunk) {
    (void)renderer;
    chunk.releaseLightingArtifacts();
    return false;
}

} // namespace lighting

