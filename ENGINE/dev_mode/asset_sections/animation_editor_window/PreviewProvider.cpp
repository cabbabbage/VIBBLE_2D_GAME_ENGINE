#include "PreviewProvider.hpp"

#include <SDL.h>
#include <SDL_image.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <cmath>
#include <nlohmann/json.hpp>
#include <system_error>
#include <numeric>
#include <vector>

#include "AnimationDocument.hpp"
#include "string_utils.hpp"

namespace {

using SurfacePtr = std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)>;
using animation_editor::strings::has_numeric_stem;

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

SurfacePtr flip_vertical(SDL_Surface* surface) {
    if (!surface) {
        return SurfacePtr(nullptr, SDL_FreeSurface);
    }
    SurfacePtr flipped = make_surface_ptr(SDL_CreateRGBSurfaceWithFormat(0, surface->w, surface->h, 32, SDL_PIXELFORMAT_RGBA32));
    if (!flipped) {
        return SurfacePtr(nullptr, SDL_FreeSurface);
    }

    if (SDL_MUSTLOCK(surface)) SDL_LockSurface(surface);
    if (SDL_MUSTLOCK(flipped.get())) SDL_LockSurface(flipped.get());

    const int bytes_per_pixel = 4;
    for (int y = 0; y < surface->h; ++y) {
        const Uint8* src_row = static_cast<const Uint8*>(surface->pixels) + y * surface->pitch;
        Uint8* dst_row = static_cast<Uint8*>(flipped->pixels) + (surface->h - 1 - y) * flipped->pitch;
        std::memcpy(dst_row, src_row, static_cast<size_t>(surface->w) * bytes_per_pixel);
    }

    if (SDL_MUSTLOCK(surface)) SDL_UnlockSurface(surface);
    if (SDL_MUSTLOCK(flipped.get())) SDL_UnlockSurface(flipped.get());

    return flipped;
}

std::string lowercase_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

const std::array<float, 5> kSpeedOptions{0.25f, 0.5f, 1.0f, 2.0f, 4.0f};

float normalize_speed_multiplier(float raw) {
    if (!std::isfinite(raw) || raw <= 0.0f) {
        return 1.0f;
    }
    float best = kSpeedOptions[0];
    float best_diff = std::fabs(best - raw);
    for (float option : kSpeedOptions) {
        float diff = std::fabs(option - raw);
        if (diff < best_diff) {
            best_diff = diff;
            best = option;
        }
    }
    return best;
}

std::vector<int> build_speed_sequence(int frame_count, float multiplier) {
    std::vector<int> sequence;
    if (frame_count <= 0) {
        return sequence;
    }
    float speed = normalize_speed_multiplier(multiplier);
    if (speed < 1.0f) {
        int repeat = std::max(1, static_cast<int>(std::lround(1.0f / speed)));
        sequence.reserve(static_cast<std::size_t>(frame_count) * repeat);
        for (int idx = 0; idx < frame_count; ++idx) {
            for (int r = 0; r < repeat; ++r) {
                sequence.push_back(idx);
            }
        }
        return sequence;
    }
    if (speed > 1.0f) {
        int step = std::max(1, static_cast<int>(std::lround(speed)));
        for (int idx = 0; idx < frame_count; idx += step) {
            sequence.push_back(idx);
        }
        if (sequence.empty()) {
            sequence.push_back(frame_count - 1);
        } else if (sequence.back() != frame_count - 1) {
            sequence.push_back(frame_count - 1);
        }
        return sequence;
    }
    sequence.resize(frame_count);
    std::iota(sequence.begin(), sequence.end(), 0);
    return sequence;
}

float parse_speed_multiplier(const nlohmann::json& payload) {
    try {
        if (payload.contains("speed_multiplier") && payload["speed_multiplier"].is_number()) {
            return normalize_speed_multiplier(payload["speed_multiplier"].get<float>());
        }
        if (payload.contains("speed_factor") && payload["speed_factor"].is_number()) {
            return normalize_speed_multiplier(payload["speed_factor"].get<float>());
        }
    } catch (...) {
    }
    return 1.0f;
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

    ResolvedAnimation resolved = resolve_animation(animation_id, 0);
    std::string signature = resolved.signature.empty() ? std::string{"anim:"} + animation_id : resolved.signature;

    auto it = cache_.find(animation_id);
    if (it != cache_.end()) {
        if (it->second.renderer == renderer && it->second.signature == signature && it->second.texture) {
            return it->second.texture.get();
        }
    }

    std::shared_ptr<SDL_Texture> texture = build_texture_from_resolved(renderer, resolved);
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

    ResolvedAnimation resolved = resolve_animation(animation_id, 0);
    std::string signature = resolved.signature.empty() ? std::string{"anim:"} + animation_id : resolved.signature;

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

    std::vector<std::shared_ptr<SDL_Texture>> textures = build_frame_textures(renderer, resolved);
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

int PreviewProvider::get_frame_count(const std::string& animation_id) const {
    ResolvedAnimation resolved = resolve_animation(animation_id, 0);
    return static_cast<int>(resolved.frames.size());
}

std::shared_ptr<SDL_Texture> PreviewProvider::build_texture(SDL_Renderer* renderer,
                                                            const std::string& animation_id, int depth) {
    if (!renderer || !document_) {
        return nullptr;
    }
    if (depth > 8) {
        return nullptr;
    }
    ResolvedAnimation resolved = resolve_animation(animation_id, depth);
    return build_texture_from_resolved(renderer, resolved);
}

std::shared_ptr<SDL_Texture> PreviewProvider::build_texture_from_resolved(SDL_Renderer* renderer,
                                                                         const ResolvedAnimation& resolved) {
    if (!renderer || resolved.frames.empty()) {
        return nullptr;
    }

    const FrameImageRequest& request = resolved.frames.front();
    if (request.path.empty()) {
        return nullptr;
    }

    SurfacePtr surface = load_surface_rgba(request.path);
    if (!surface) {
        return nullptr;
    }

    if (request.flip_x) {
        SurfacePtr flipped = flip_horizontal(surface.get());
        if (flipped) {
            surface = std::move(flipped);
        }
    }
    if (request.flip_y) {
        SurfacePtr flipped = flip_vertical(surface.get());
        if (flipped) {
            surface = std::move(flipped);
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
                                                                                const ResolvedAnimation& resolved) {
    std::vector<std::shared_ptr<SDL_Texture>> textures;
    if (!renderer) {
        return textures;
    }
    if (resolved.frames.empty()) {
        return textures;
    }

    textures.reserve(resolved.frames.size());
    for (const auto& request : resolved.frames) {
        if (request.path.empty()) {
            textures.emplace_back();
            continue;
        }

        SurfacePtr surface = load_surface_rgba(request.path);
        if (!surface) {
            textures.emplace_back();
            continue;
        }

        SurfacePtr temp(nullptr, SDL_FreeSurface);
        SDL_Surface* source_surface = surface.get();
        if (request.flip_x) {
            temp = flip_horizontal(surface.get());
            if (temp) {
                surface = std::move(temp);
                source_surface = surface.get();
            }
        }
        if (request.flip_y) {
            temp = flip_vertical(source_surface);
            if (temp) {
                surface = std::move(temp);
                source_surface = surface.get();
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

PreviewProvider::ResolvedAnimation PreviewProvider::resolve_animation(const std::string& animation_id, int depth) const {
    ResolvedAnimation result;
    result.signature = std::string{"anim:"} + animation_id;
    if (!document_ || animation_id.empty() || depth > 16) {
        return result;
    }

    auto payload_dump = document_->animation_payload(animation_id);
    std::string payload_signature = payload_dump.has_value() ? *payload_dump : std::string{};

    if (!payload_dump.has_value() || payload_dump->empty()) {
        std::filesystem::path folder = resolve_asset_root();
        if (!folder.empty()) {
            folder /= animation_id;
        }
        std::vector<std::filesystem::path> paths = find_frame_sequence(folder, 0);
        result.frames.reserve(paths.size());
        for (const auto& path : paths) {
            result.frames.push_back(FrameImageRequest{path, false, false});
        }
        result.signature = std::string{"folder:"} + folder.generic_string();
        for (const auto& path : paths) {
            result.signature.push_back('|');
            result.signature.append(path.filename().generic_string());
        }
        return result;
    }

    nlohmann::json payload = nlohmann::json::parse(*payload_dump, nullptr, false);
    if (payload.is_discarded() || !payload.is_object()) {
        result.signature = payload_signature + "|invalid";
        return result;
    }

    const nlohmann::json* source = payload.contains("source") && payload["source"].is_object() ? &payload["source"] : nullptr;
    std::string kind = source ? source->value("kind", std::string{"folder"}) : std::string{"folder"};

    bool reverse = payload.value("reverse_source", false);
    bool flip_x = payload.value("flipped_source", false);
    bool flip_y = false;
    bool flip_movement_x = false;
    bool flip_movement_y = false;
    if (kind == "animation" && payload.contains("derived_modifiers") && payload["derived_modifiers"].is_object()) {
        const auto& modifiers = payload["derived_modifiers"];
        reverse = modifiers.value("reverse", reverse);
        flip_x = modifiers.value("flipX", flip_x);
        flip_y = modifiers.value("flipY", false);
        flip_movement_x = modifiers.value("flipMovementX", flip_movement_x);
        flip_movement_y = modifiers.value("flipMovementY", flip_movement_y);
    }

    float speed_multiplier = parse_speed_multiplier(payload);

    if (kind == "animation") {
        std::string reference = source ? source->value("name", std::string{}) : std::string{};
        if (reference.empty() && source) {
            reference = source->value("path", std::string{});
        }
        reference = strings::trim_copy(reference);
        if (reference.empty() || reference == animation_id) {
            result.signature = payload_signature + "|missing_ref";
            return result;
        }

        ResolvedAnimation nested = resolve_animation(reference, depth + 1);
        result.frames = nested.frames;
        result.signature = payload_signature + "|child{" + nested.signature + "}";

        if (!result.frames.empty() && speed_multiplier != 1.0f) {
            auto sequence = build_speed_sequence(static_cast<int>(result.frames.size()), speed_multiplier);
            if (!sequence.empty()) {
                std::vector<FrameImageRequest> adjusted;
                adjusted.reserve(sequence.size());
                for (int idx : sequence) {
                    if (idx >= 0 && idx < static_cast<int>(result.frames.size())) {
                        adjusted.push_back(result.frames[static_cast<std::size_t>(idx)]);
                    }
                }
                if (!adjusted.empty()) {
                    result.frames.swap(adjusted);
                }
            }
        }

        if (reverse && !result.frames.empty()) {
            std::reverse(result.frames.begin(), result.frames.end());
        }
        for (auto& frame : result.frames) {
            frame.flip_x = frame.flip_x ^ flip_x;
            frame.flip_y = frame.flip_y ^ flip_y;
        }

        result.signature += "|mods:";
        result.signature.push_back(reverse ? '1' : '0');
        result.signature.push_back(flip_x ? '1' : '0');
        result.signature.push_back(flip_y ? '1' : '0');
        result.signature.push_back(flip_movement_x ? '1' : '0');
        result.signature.push_back(flip_movement_y ? '1' : '0');
        return result;
    }

    int frames = payload.value("number_of_frames", 0);
    if (frames < 0) frames = 0;

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

    std::vector<std::filesystem::path> paths = find_frame_sequence(folder, frames);
    std::vector<std::filesystem::path> adjusted_paths;
    if (!paths.empty()) {
        auto sequence = build_speed_sequence(static_cast<int>(paths.size()), speed_multiplier);
        adjusted_paths.reserve(sequence.size());
        for (int idx : sequence) {
            if (idx >= 0 && idx < static_cast<int>(paths.size())) {
                adjusted_paths.push_back(paths[static_cast<std::size_t>(idx)]);
            }
        }
    }
    const std::vector<std::filesystem::path>& final_paths =
        adjusted_paths.empty() ? paths : adjusted_paths;

    result.frames.reserve(final_paths.size());
    for (const auto& path : final_paths) {
        FrameImageRequest request{path, flip_x, flip_y};
        result.frames.push_back(request);
    }
    if (reverse && !result.frames.empty()) {
        std::reverse(result.frames.begin(), result.frames.end());
    }

    result.signature = payload_signature + "|files:";
    for (const auto& path : final_paths) {
        result.signature.append(path.filename().generic_string());
        result.signature.push_back(';');
    }
    result.signature.push_back('|');
    result.signature.append("mods:");
    result.signature.push_back(reverse ? '1' : '0');
    result.signature.push_back(flip_x ? '1' : '0');
    result.signature.push_back(flip_y ? '1' : '0');
    return result;
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
    std::vector<std::filesystem::path> numeric_frames;
    std::vector<std::filesystem::path> fallback_sequence;
    bool has_fallback_sequence = false;
    std::error_code ec;

    if (frames > 0) {
        fallback_sequence.reserve(frames);
        std::filesystem::path fallback;
        for (int i = 0; i < frames; ++i) {
            std::filesystem::path candidate = folder / (std::to_string(i) + ".png");
            if (std::filesystem::exists(candidate, ec)) {
                fallback_sequence.push_back(candidate);
                if (fallback.empty()) {
                    fallback = candidate;
                }
            } else {
                fallback_sequence.emplace_back();
            }
        }
        if (!fallback.empty()) {
            for (auto& path : fallback_sequence) {
                if (path.empty()) {
                    path = fallback;
                }
            }
            has_fallback_sequence = true;
        } else {
            fallback_sequence.clear();
        }
    }

    if (!std::filesystem::exists(folder, ec) || !std::filesystem::is_directory(folder, ec)) {
        return has_fallback_sequence ? fallback_sequence : std::vector<std::filesystem::path>{};
    }

    for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        const std::filesystem::path& path = entry.path();
        std::string ext = lowercase_copy(path.extension().string());
        if (ext != ".png") continue;
        if (!has_numeric_stem(path)) continue;
        numeric_frames.push_back(path);
    }

    if (numeric_frames.empty()) {
        return has_fallback_sequence ? fallback_sequence : std::vector<std::filesystem::path>{};
    }

    std::sort(numeric_frames.begin(), numeric_frames.end(),
              [](const std::filesystem::path& a, const std::filesystem::path& b) {
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
        const int available = static_cast<int>(numeric_frames.size());
        const int target = std::max(frames, available);
        std::vector<std::filesystem::path> sequence;
        sequence.reserve(target);
        for (int i = 0; i < target; ++i) {
            if (i < available) {
                sequence.push_back(numeric_frames[i]);
            } else {
                sequence.push_back(numeric_frames.back());
            }
        }
        return sequence;
    }

    return numeric_frames;
}

}
