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
    if (!renderer || !mask) {
        return false;
    }

    if (!ensureDirectories()) {
        return false;
    }

    MeasureResult mask_pixels = readRgba(renderer, mask);
    if (!mask_pixels.success()) {
        chunk.needs_retry = true;
        return false;
    }

    const auto mask_path = maskPath(chunk);
    std::ofstream mask_file(mask_path, std::ios::binary | std::ios::trunc);
    if (!mask_file) {
        vibble::log::warn("[Lighting] Unable to open mask cache file for writing");
        return false;
    }
    mask_file.write(reinterpret_cast<const char*>(mask_pixels.pixels.data()),
                    static_cast<std::streamsize>(mask_pixels.pixels.size()));

    nlohmann::json entry;
    entry["i"]          = chunk.i;
    entry["j"]          = chunk.j;
    entry["width"]      = mask_pixels.width;
    entry["height"]     = mask_pixels.height;
    entry["mask"]       = mask_path.filename().string();
    entry["min_strength"] = chunk.lighting.min_static_avg_strength;
    entry["max_strength"] = chunk.lighting.max_static_avg_strength;

    manifest_[chunkKey(chunk)] = std::move(entry);
    return persistManifest();
}

bool LightingCache::loadChunk(SDL_Renderer* renderer, world::Chunk& chunk) {
    if (!renderer) {
        return false;
    }

    if (!manifest_.is_object()) {
        return false;
    }

    const std::string key = chunkKey(chunk);
    const auto        it  = manifest_.find(key);
    if (it == manifest_.end() || !it->is_object()) {
        return false;
    }
    const auto& entry = *it;

    const auto mask_filename = entry.value("mask", std::string{});
    if (mask_filename.empty()) {
        return false;
    }

    const auto mask_path = root_ / mask_filename;
    std::ifstream mask_file(mask_path, std::ios::binary);
    if (!mask_file) {
        return false;
    }

    const int width  = entry.value("width", 0);
    const int height = entry.value("height", 0);
    if (width <= 0 || height <= 0) {
        return false;
    }

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    mask_file.read(reinterpret_cast<char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
    if (!mask_file) {
        return false;
    }

    chunk.releaseLightingArtifacts();

    SDL_Texture* texture = SDL_CreateTexture(renderer,
                                             SDL_PIXELFORMAT_RGBA32,
                                             SDL_TEXTUREACCESS_STATIC,
                                             width,
                                             height);
    if (!texture) {
        vibble::log::warn(std::string{"[Lighting] Failed to allocate cached mask texture: "} + SDL_GetError());
        return false;
    }

    if (SDL_UpdateTexture(texture, nullptr, pixels.data(), width * 4) != 0) {
        vibble::log::warn(std::string{"[Lighting] Failed to upload cached mask texture: "} + SDL_GetError());
        SDL_DestroyTexture(texture);
        return false;
    }
    const SDL_BlendMode runtime_blend = PreloadInputs::computeRuntimeLightBlendMode();
    if (SDL_SetTextureBlendMode(texture, runtime_blend) != 0) {
        vibble::log::warn(std::string{"[Lighting] Failed to configure cached mask blend mode: "} + SDL_GetError());
    }

    chunk.static_light_mask                  = texture;
    chunk.lighting_preloaded                 = true;
    chunk.static_clean                       = true;
    chunk.lighting_dirty                     = false;
    chunk.lighting.min_static_avg_strength   = entry.value("min_strength", chunk.lighting.min_static_avg_strength);
    chunk.lighting.max_static_avg_strength   = entry.value("max_strength", chunk.lighting.max_static_avg_strength);
    if (chunk.lighting.max_static_avg_strength < chunk.lighting.min_static_avg_strength) {
        std::swap(chunk.lighting.max_static_avg_strength, chunk.lighting.min_static_avg_strength);
    }
    chunk.lighting.min_static_avg_strength = std::clamp(chunk.lighting.min_static_avg_strength, 0.0f, 1.0f);
    chunk.lighting.max_static_avg_strength = std::clamp(chunk.lighting.max_static_avg_strength, 0.0f, 1.0f);
    chunk.needs_retry                        = false;

    return true;
}

} // namespace lighting

