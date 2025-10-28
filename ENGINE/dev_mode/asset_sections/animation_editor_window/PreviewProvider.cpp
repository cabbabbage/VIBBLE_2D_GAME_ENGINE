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
#include "string_utils.hpp"

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
    SurfacePtr flipped = make_surface_ptr( SDL_CreateRGBSurfaceWithFormat(0, surface->w, surface->h, 32, SDL_PIXELFORMAT_RGBA32));
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
            std::memcpy(dst_row + x * bytes_per_pixel, src_row + (surface->w - 1 - x) * bytes_per_pixel, bytes_per_pixel);
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

std::string lowercase_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

}

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

SDL_Texture* PreviewProvider::get_frame_texture(SDL_Renderer* renderer, const std::string& animation_id, int frame_index) {
    if (!renderer || animation_id.empty() || frame_index < 0) {
        return nullptr;
    }

    if (!document_) {
        frame_cache_.erase(animation_id);
        return nullptr;
    }

    asset_root_ = resolve_asset_root();

    auto payload = document_->animation_payload(animation_id);
    std::string signature = payload.has_value() ? *payload : std::string{};

    auto it = frame_cache_.find(animation_id);
    if (it != frame_cache_.end()) {
        FrameCacheEntry& entry = it->second;
        if (entry.renderer == renderer && entry.signature == signature) {
            if (frame_index < static_cast<int>(entry.textures.size())) {
                const auto& tex = entry.textures[frame_index];
                if (tex) {
                    return tex.get();
                }
            }
        }
    }

    std::vector<std::shared_ptr<SDL_Texture>> textures = build_frame_textures(renderer, animation_id);
    if (textures.empty()) {
        frame_cache_.erase(animation_id);
        return nullptr;
    }

    FrameCacheEntry entry;
    entry.renderer = renderer;
    entry.signature = std::move(signature);
    entry.textures = std::move(textures);
    auto [stored_it, inserted] = frame_cache_.insert_or_assign(animation_id, std::move(entry));
    FrameCacheEntry& stored_entry = stored_it->second;
    if (frame_index < 0 || frame_index >= static_cast<int>(stored_entry.textures.size())) {
        return nullptr;
    }
    const auto& tex = stored_entry.textures[frame_index];
    return tex ? tex.get() : nullptr;
}

void PreviewProvider::invalidate(const std::string& animation_id) {
    cache_.erase(animation_id);
    frame_cache_.erase(animation_id);
}

void PreviewProvider::invalidate_all() {
    cache_.clear();
    frame_cache_.clear();
}

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

    auto should_treat_as_absolute = [&](const std::filesystem::path& requested) {
        if (requested.is_absolute()) {
            return true;
        }

        std::string requested_str = lowercase_copy(requested.generic_string());
        if (requested_str.rfind("src/", 0) == 0) {
            return true;
        }

        if (!asset_root_.empty()) {
            std::string root_str = lowercase_copy(asset_root_.generic_string());
            if (!root_str.empty()) {
                if (requested_str == root_str) {
                    return true;
                }
                std::string root_with_sep = root_str + "/";
                if (requested_str.rfind(root_with_sep, 0) == 0) {
                    return true;
                }
            }
        }

        return false;
};

    std::filesystem::path requested = relative_path;
    if (should_treat_as_absolute(requested)) {
        folder = requested;
    } else if (!relative_path.empty()) {
        if (!folder.empty()) {
            folder /= requested;
        } else {
            folder = requested;
        }
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

std::vector<std::shared_ptr<SDL_Texture>> PreviewProvider::build_frame_textures(SDL_Renderer* renderer,
                                                                                const std::string& animation_id,
                                                                                int depth) {
    std::vector<std::shared_ptr<SDL_Texture>> textures;
    if (!renderer || !document_) {
        return textures;
    }
    if (depth > 8) {
        return textures;
    }

    std::vector<FrameImageRequest> requests = gather_frame_requests(animation_id, depth, false);
    if (requests.empty()) {
        return textures;
    }

    textures.reserve(requests.size());
    for (const auto& request : requests) {
        if (request.path.empty()) {
            textures.emplace_back();
            continue;
        }
        SurfacePtr surface = load_surface_rgba(request.path);
        if (!surface) {
            textures.emplace_back();
            continue;
        }
        SurfacePtr flipped_surface(nullptr, SDL_FreeSurface);
        SDL_Surface* source_surface = surface.get();
        if (request.flipped) {
            flipped_surface = flip_horizontal(surface.get());
            if (flipped_surface) {
                source_surface = flipped_surface.get();
            }
        }
        SDL_Texture* raw = SDL_CreateTextureFromSurface(renderer, source_surface);
        if (!raw) {
            textures.emplace_back();
            continue;
        }
        SDL_SetTextureBlendMode(raw, SDL_BLENDMODE_BLEND);
        textures.emplace_back(raw, SDL_DestroyTexture);
    }

    return textures;
}

std::vector<PreviewProvider::FrameImageRequest> PreviewProvider::gather_frame_requests(const std::string& animation_id,
                                                                                       int depth,
                                                                                       bool inherited_flip) {
    std::vector<FrameImageRequest> requests;
    if (!document_ || depth > 8) {
        return requests;
    }

    auto payload_dump = document_->animation_payload(animation_id);
    if (!payload_dump || payload_dump->empty()) {
        std::filesystem::path folder = resolve_asset_root();
        if (!folder.empty()) {
            folder /= animation_id;
        }
        if (folder.empty()) {
            return requests;
        }
        std::vector<std::filesystem::path> paths = find_frame_sequence(folder, 0);
        requests.reserve(paths.size());
        for (const auto& path : paths) {
            requests.push_back(FrameImageRequest{path, inherited_flip});
        }
        return requests;
    }

    nlohmann::json payload = nlohmann::json::parse(*payload_dump, nullptr, false);
    if (payload.is_discarded() || !payload.is_object()) {
        return requests;
    }

    return gather_frame_requests_from_payload(animation_id, payload, depth, inherited_flip);
}

std::vector<PreviewProvider::FrameImageRequest> PreviewProvider::gather_frame_requests_from_payload(
    const std::string& animation_id, const nlohmann::json& payload, int depth, bool inherited_flip) {
    std::vector<FrameImageRequest> requests;
    bool flipped_source = payload.value("flipped_source", false);
    bool effective_flip = inherited_flip ^ flipped_source;
    int frames = payload.value("number_of_frames", 0);
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
        reference = strings::trim_copy(reference);
        if (!reference.empty() && reference != animation_id) {
            std::vector<FrameImageRequest> nested = gather_frame_requests(reference, depth + 1, effective_flip);
            requests.insert(requests.end(), nested.begin(), nested.end());
        }
        return requests;
    }

    std::string relative_path = source ? source->value("path", std::string{}) : std::string{};
    if (relative_path.empty()) {
        relative_path = animation_id;
    }

    std::filesystem::path folder = asset_root_;

    auto should_treat_as_absolute = [&](const std::filesystem::path& requested) {
        if (requested.is_absolute()) {
            return true;
        }

        std::string requested_str = lowercase_copy(requested.generic_string());
        if (requested_str.rfind("src/", 0) == 0) {
            return true;
        }

        if (!asset_root_.empty()) {
            std::string root_str = lowercase_copy(asset_root_.generic_string());
            if (!root_str.empty()) {
                if (requested_str == root_str) {
                    return true;
                }
                std::string root_with_sep = root_str + "/";
                if (requested_str.rfind(root_with_sep, 0) == 0) {
                    return true;
                }
            }
        }

        return false;
    };

    std::filesystem::path requested = relative_path;
    if (should_treat_as_absolute(requested)) {
        folder = requested;
    } else if (!relative_path.empty()) {
        if (!folder.empty()) {
            folder /= requested;
        } else {
            folder = requested;
        }
    }

    if (folder.empty()) {
        return requests;
    }

    std::vector<std::filesystem::path> paths = find_frame_sequence(folder, frames);
    requests.reserve(paths.size());
    for (const auto& path : paths) {
        requests.push_back(FrameImageRequest{path, effective_flip});
    }

    return requests;
}

std::filesystem::path PreviewProvider::resolve_asset_root() const {
    if (!document_) {
        return {};
    }
    const std::filesystem::path& root = document_->asset_root();
    if (!root.empty()) {
        return root;
    }
    std::filesystem::path info = document_->info_path();
    if (!info.empty()) {
        return info.parent_path();
    }
    return {};
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
        std::string ext = lowercase_copy(path.extension().string());
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

std::vector<std::filesystem::path> PreviewProvider::find_frame_sequence(const std::filesystem::path& folder,
                                                                        int frames) const {
    std::vector<std::filesystem::path> sequence;
    std::error_code ec;

    if (frames > 0) {
        sequence.reserve(frames);
        std::filesystem::path fallback;
        for (int i = 0; i < frames; ++i) {
            std::filesystem::path candidate = folder / (std::to_string(i) + ".png");
            if (std::filesystem::exists(candidate, ec)) {
                sequence.push_back(candidate);
                if (fallback.empty()) {
                    fallback = candidate;
                }
            } else {
                sequence.emplace_back();
            }
        }
        if (!fallback.empty()) {
            for (auto& path : sequence) {
                if (path.empty()) {
                    path = fallback;
                }
            }
            return sequence;
        }
        sequence.clear();
    }

    if (!std::filesystem::exists(folder, ec) || !std::filesystem::is_directory(folder, ec)) {
        return sequence;
    }

    std::vector<std::filesystem::path> numbered;
    for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        const std::filesystem::path& path = entry.path();
        std::string ext = lowercase_copy(path.extension().string());
        if (ext != ".png") continue;
        if (!has_numeric_stem(path)) continue;
        numbered.push_back(path);
    }

    if (numbered.empty()) {
        return sequence;
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

    if (frames > 0) {
        sequence.reserve(frames);
        for (int i = 0; i < frames; ++i) {
            sequence.push_back(numbered[std::min(i, static_cast<int>(numbered.size()) - 1)]);
        }
        return sequence;
    }

    return numbered;
}

}

