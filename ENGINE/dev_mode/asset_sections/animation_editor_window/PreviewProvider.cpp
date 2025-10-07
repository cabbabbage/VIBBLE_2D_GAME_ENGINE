#include "PreviewProvider.hpp"

#include <SDL.h>
#include <SDL_image.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <nlohmann/json.hpp>
#include <system_error>
#include <vector>

#include "AnimationDocument.hpp"

namespace {

using SurfacePtr = std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)>;

SurfacePtr make_surface_ptr(SDL_Surface* surface) { return SurfacePtr(surface, SDL_FreeSurface); }

SurfacePtr load_surface_rgba(const std::filesystem::path& path) {
    SDL_Surface* loaded = IMG_Load(path.string().c_str());
    if (!loaded) {
        return SurfacePtr(nullptr, SDL_FreeSurface);
    }
    SDL_Surface* converted = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(loaded);
    if (!converted) {
        return SurfacePtr(nullptr, SDL_FreeSurface);
    }
    return make_surface_ptr(converted);
}

SurfacePtr flip_horizontal(SDL_Surface* surface) {
    if (!surface) {
        return SurfacePtr(nullptr, SDL_FreeSurface);
    }
    SurfacePtr flipped = make_surface_ptr(
        SDL_CreateRGBSurfaceWithFormat(0, surface->w, surface->h, 32, SDL_PIXELFORMAT_RGBA32));
    if (!flipped) {
        return SurfacePtr(nullptr, SDL_FreeSurface);
    }

    if (SDL_MUSTLOCK(surface)) SDL_LockSurface(surface);
    if (SDL_MUSTLOCK(flipped.get())) SDL_LockSurface(flipped.get());

    const int bytes_per_pixel = 4;
    for (int y = 0; y < surface->h; ++y) {
        const Uint8* src_row = static_cast<const Uint8*>(surface->pixels) + y * surface->pitch;
        Uint8* dst_row = static_cast<Uint8*>(flipped->pixels) + y * flipped->pitch;
        for (int x = 0; x < surface->w; ++x) {
            std::memcpy(dst_row + x * bytes_per_pixel,
                        src_row + (surface->w - 1 - x) * bytes_per_pixel, bytes_per_pixel);
        }
    }

    if (SDL_MUSTLOCK(surface)) SDL_UnlockSurface(surface);
    if (SDL_MUSTLOCK(flipped.get())) SDL_UnlockSurface(flipped.get());

    return flipped;
}

bool has_numeric_stem(const std::filesystem::path& path) {
    std::string stem = path.stem().string();
    if (stem.empty()) return false;
    return std::all_of(stem.begin(), stem.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; });
}

std::string to_lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

}  // namespace

namespace animation_editor {

PreviewProvider::PreviewProvider() = default;

void PreviewProvider::set_document(std::shared_ptr<AnimationDocument> document) {
    document_ = std::move(document);
    invalidate_all();
    asset_root_.clear();
    asset_root_ = resolve_asset_root();
}

SDL_Texture* PreviewProvider::get_preview_texture(SDL_Renderer* renderer, const std::string& animation_id) {
    if (!renderer || animation_id.empty()) {
        return nullptr;
    }

    if (!document_) {
        cache_.erase(animation_id);
        return nullptr;
    }

    asset_root_ = resolve_asset_root();

    auto payload = document_->animation_payload(animation_id);
    std::string signature = payload.has_value() ? *payload : std::string{};

    auto it = cache_.find(animation_id);
    if (it != cache_.end()) {
        if (it->second.renderer == renderer && it->second.signature == signature && it->second.texture) {
            return it->second.texture.get();
        }
    }

    std::shared_ptr<SDL_Texture> texture = build_texture(renderer, animation_id);
    if (!texture) {
        cache_.erase(animation_id);
        return nullptr;
    }

    CacheEntry entry;
    entry.renderer = renderer;
    entry.signature = std::move(signature);
    entry.texture = std::move(texture);
    cache_[animation_id] = std::move(entry);
    return cache_[animation_id].texture.get();
}

void PreviewProvider::invalidate(const std::string& animation_id) { cache_.erase(animation_id); }

void PreviewProvider::invalidate_all() { cache_.clear(); }

std::shared_ptr<SDL_Texture> PreviewProvider::build_texture(SDL_Renderer* renderer,
                                                            const std::string& animation_id, int depth) {
    if (!renderer || !document_) {
        return nullptr;
    }
    if (depth > 8) {
        return nullptr;
    }

    auto payload_dump = document_->animation_payload(animation_id);
    if (!payload_dump || payload_dump->empty()) {
        std::filesystem::path folder = resolve_asset_root();
        folder /= animation_id;
        return load_folder_texture(renderer, folder, 0, false);
    }

    nlohmann::json payload = nlohmann::json::parse(*payload_dump, nullptr, false);
    if (payload.is_discarded() || !payload.is_object()) {
        return nullptr;
    }

    return build_texture_from_payload(renderer, animation_id, payload, depth);
}

std::shared_ptr<SDL_Texture> PreviewProvider::build_texture_from_payload(SDL_Renderer* renderer,
                                                                        const std::string& animation_id,
                                                                        const nlohmann::json& payload, int depth) {
    const bool flipped = payload.value("flipped_source", false);
    int frames = payload.value("number_of_frames", 1);
    if (frames < 0) frames = 0;

    const nlohmann::json* source = nullptr;
    if (payload.contains("source") && payload["source"].is_object()) {
        source = &payload["source"];
    }

    std::string kind = source ? source->value("kind", std::string{"folder"}) : std::string{"folder"};

    if (kind == "animation") {
        std::string reference = source ? source->value("name", std::string{}) : std::string{};
        if (reference.empty() && source) {
            reference = source->value("path", std::string{});
        }
        if (reference.empty() || reference == animation_id) {
            return nullptr;
        }
        return build_texture(renderer, reference, depth + 1);
    }

    std::string relative_path = source ? source->value("path", std::string{}) : std::string{};
    if (relative_path.empty()) {
        relative_path = animation_id;
    }

    std::filesystem::path folder = asset_root_;
    if (!relative_path.empty()) {
        folder /= relative_path;
    }

    return load_folder_texture(renderer, folder, frames, flipped);
}

std::shared_ptr<SDL_Texture> PreviewProvider::load_folder_texture(SDL_Renderer* renderer,
                                                                  const std::filesystem::path& folder, int frames,
                                                                  bool flipped) const {
    std::filesystem::path frame_path = find_first_frame(folder, frames);
    if (frame_path.empty()) {
        return nullptr;
    }

    SurfacePtr surface = load_surface_rgba(frame_path);
    if (!surface) {
        return nullptr;
    }

    if (flipped) {
        SurfacePtr flipped_surface = flip_horizontal(surface.get());
        if (flipped_surface) {
            surface = std::move(flipped_surface);
        }
    }

    SDL_Texture* raw = SDL_CreateTextureFromSurface(renderer, surface.get());
    if (!raw) {
        return nullptr;
    }
    SDL_SetTextureBlendMode(raw, SDL_BLENDMODE_BLEND);
    return std::shared_ptr<SDL_Texture>(raw, SDL_DestroyTexture);
}

std::filesystem::path PreviewProvider::resolve_asset_root() const {
    if (!document_) {
        return {};
    }
    std::filesystem::path info = document_->info_path();
    if (info.empty()) {
        return {};
    }
    return info.parent_path();
}

std::filesystem::path PreviewProvider::find_first_frame(const std::filesystem::path& folder, int frames) const {
    std::error_code ec;

    if (frames > 0) {
        for (int i = 0; i < frames; ++i) {
            std::filesystem::path candidate = folder / (std::to_string(i) + ".png");
            if (std::filesystem::exists(candidate, ec)) {
                return candidate;
            }
        }
    }

    if (!std::filesystem::exists(folder, ec) || !std::filesystem::is_directory(folder, ec)) {
        return {};
    }

    std::vector<std::filesystem::path> numbered;
    for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        const std::filesystem::path& path = entry.path();
        std::string ext = to_lower_copy(path.extension().string());
        if (ext != ".png") continue;
        if (!has_numeric_stem(path)) continue;
        numbered.push_back(path);
    }

    if (numbered.empty()) {
        return {};
    }

    std::sort(numbered.begin(), numbered.end(), [](const std::filesystem::path& a, const std::filesystem::path& b) {
        int lhs = 0;
        int rhs = 0;
        try {
            lhs = std::stoi(a.stem().string());
        } catch (...) {
            lhs = 0;
        }
        try {
            rhs = std::stoi(b.stem().string());
        } catch (...) {
            rhs = 0;
        }
        return lhs < rhs;
    });

    return numbered.front();
}

}  // namespace animation_editor

