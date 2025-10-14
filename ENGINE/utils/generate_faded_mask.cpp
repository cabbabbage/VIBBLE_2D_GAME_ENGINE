#include "generate_faded_mask.hpp"

#include "cache_manager.hpp"
#include "render_pipeline/ScalingLogic.hpp"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <limits>
#include <nlohmann/json.hpp>
#include <queue>
#include <vector>

namespace {

constexpr int   kCacheVersion    = 3;
constexpr float kExpansionRatio  = 0.8f;
constexpr float kFadeExponent    = 1.2f;

struct DistanceNode {
    float dist;
    int   x;
    int   y;

    bool operator<(const DistanceNode& other) const { return dist > other.dist; }
};

int compute_expand_radius(int w, int h) {
    const int max_dim = std::max(w, h);
    const float scaled = static_cast<float>(max_dim) * (kExpansionRatio * 0.5f);
    const int radius = static_cast<int>(std::ceil(scaled));
    return std::max(1, radius);
}

int compute_base_blur_radius(int expand_radius) {
    if (expand_radius <= 2) return 0;
    return std::max(1, expand_radius / 6);
}

float bell_curve_weight(float t) {
    const float clamped = std::clamp(t, 0.0f, 1.0f);
    // Smooth start/end using a cosine bell curve for softer transitions.
    const float pi = static_cast<float>(std::acos(-1.0));
    return 0.5f - 0.5f * std::cos(clamped * pi);
}

void apply_box_blur(std::vector<float>& values, int width, int height, int radius) {
    if (radius <= 0 || width <= 0 || height <= 0) return;

    std::vector<float> horizontal(width * height, 0.0f);

    std::vector<float> prefix_row(width + 1, 0.0f);
    for (int y = 0; y < height; ++y) {
        std::fill(prefix_row.begin(), prefix_row.end(), 0.0f);
        for (int x = 0; x < width; ++x) {
            prefix_row[x + 1] = prefix_row[x] + values[y * width + x];
        }

        for (int x = 0; x < width; ++x) {
            const int left   = std::max(0, x - radius);
            const int right  = std::min(width - 1, x + radius);
            const float sum  = prefix_row[right + 1] - prefix_row[left];
            const int count  = right - left + 1;
            horizontal[y * width + x] = sum / static_cast<float>(count);
        }
    }

    std::vector<float> blurred(width * height, 0.0f);
    std::vector<float> prefix_col(height + 1, 0.0f);
    for (int x = 0; x < width; ++x) {
        std::fill(prefix_col.begin(), prefix_col.end(), 0.0f);
        for (int y = 0; y < height; ++y) {
            prefix_col[y + 1] = prefix_col[y] + horizontal[y * width + x];
        }

        for (int y = 0; y < height; ++y) {
            const int top     = std::max(0, y - radius);
            const int bottom  = std::min(height - 1, y + radius);
            const float sum   = prefix_col[bottom + 1] - prefix_col[top];
            const int count   = bottom - top + 1;
            blurred[y * width + x] = sum / static_cast<float>(count);
        }
    }

    values.swap(blurred);
}

std::string variant_folder(const std::filesystem::path& base,
                           std::size_t index,
                           const std::vector<int>& scale_steps) {
    std::filesystem::path folder(base);
    if (!scale_steps.empty() && index < scale_steps.size()) {
        folder /= "scale_" + std::to_string(scale_steps[index]);
    } else {
        const int fallback = render_pipeline::ScalingLogic::ScalePercent(index);
        folder /= "scale_" + std::to_string(fallback);
    }
    return folder.string();
}

SDL_Surface* make_rgba_surface(int w, int h) {
    return SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
}

SDL_Surface* generate_mask_surface(SDL_Surface* source) {
    if (!source) return nullptr;

    SDL_Surface* src_rgba = SDL_ConvertSurfaceFormat(source, SDL_PIXELFORMAT_RGBA32, 0);
    if (!src_rgba) return nullptr;

    const int width  = std::max(1, src_rgba->w);
    const int height = std::max(1, src_rgba->h);

    const int expand_radius    = compute_expand_radius(width, height);
    const int base_blur_radius = compute_base_blur_radius(expand_radius);

    const int expanded_w = width + expand_radius * 2;
    const int expanded_h = height + expand_radius * 2;

    SDL_Surface* mask = make_rgba_surface(expanded_w, expanded_h);
    if (!mask) {
        SDL_FreeSurface(src_rgba);
        return nullptr;
    }

    if (SDL_LockSurface(src_rgba) != 0) {
        SDL_FreeSurface(src_rgba);
        SDL_FreeSurface(mask);
        return nullptr;
    }

    if (SDL_LockSurface(mask) != 0) {
        SDL_UnlockSurface(src_rgba);
        SDL_FreeSurface(src_rgba);
        SDL_FreeSurface(mask);
        return nullptr;
    }

    const Uint8* src_pixels = static_cast<const Uint8*>(src_rgba->pixels);
    const int src_pitch     = src_rgba->pitch;

    std::vector<float> distance(expanded_w * expanded_h, std::numeric_limits<float>::infinity());

    std::priority_queue<DistanceNode> queue;
    bool has_opaque = false;

    for (int y = 0; y < height; ++y) {
        const Uint8* row = src_pixels + y * src_pitch;
        for (int x = 0; x < width; ++x) {
            const Uint8 alpha = row[x * 4 + 3];
            const int expanded_x = x + expand_radius;
            const int expanded_y = y + expand_radius;
            const int idx        = expanded_y * expanded_w + expanded_x;
            if (alpha > 0) {
                has_opaque    = true;
                distance[idx] = 0.0f;
                queue.push({0.0f, expanded_x, expanded_y});
            }
        }
    }

    if (!has_opaque) {
        Uint32* dst_pixels = static_cast<Uint32*>(mask->pixels);
        const Uint32 transparent = SDL_MapRGBA(mask->format, 0, 0, 0, 0);
        std::fill(dst_pixels, dst_pixels + expanded_w * expanded_h, transparent);
        SDL_UnlockSurface(mask);
        SDL_UnlockSurface(src_rgba);
        SDL_FreeSurface(src_rgba);
        return mask;
    }

    static const std::array<std::pair<int, int>, 8> kNeighborOffsets = {
        std::make_pair(1, 0), std::make_pair(-1, 0), std::make_pair(0, 1), std::make_pair(0, -1),
        std::make_pair(1, 1), std::make_pair(-1, 1), std::make_pair(1, -1), std::make_pair(-1, -1)
    };

    while (!queue.empty()) {
        const DistanceNode node = queue.top();
        queue.pop();

        const int idx = node.y * expanded_w + node.x;
        if (node.dist > distance[idx] + 1e-4f) continue;
        if (node.dist > static_cast<float>(expand_radius)) continue;

        for (const auto& [dx, dy] : kNeighborOffsets) {
            const int nx = node.x + dx;
            const int ny = node.y + dy;
            if (nx < 0 || ny < 0 || nx >= expanded_w || ny >= expanded_h) continue;
            const float step = (dx == 0 || dy == 0) ? 1.0f : std::sqrt(2.0f);
            const float candidate = node.dist + step;
            if (candidate > static_cast<float>(expand_radius) + 1e-4f) continue;
            const int nidx = ny * expanded_w + nx;
            if (candidate + 1e-4f < distance[nidx]) {
                distance[nidx] = candidate;
                queue.push({candidate, nx, ny});
            }
        }
    }

    const float expand_radius_f = static_cast<float>(expand_radius);

    std::vector<float> base_alpha(expanded_w * expanded_h, 0.0f);
    for (std::size_t i = 0; i < distance.size(); ++i) {
        const float d = distance[i];
        if (!std::isfinite(d) || d > expand_radius_f) continue;
        base_alpha[i] = 255.0f;
    }

    apply_box_blur(base_alpha, expanded_w, expanded_h, base_blur_radius);

    std::vector<float> alpha_values(expanded_w * expanded_h, 0.0f);
    for (std::size_t i = 0; i < distance.size(); ++i) {
        const float d = distance[i];
        if (!std::isfinite(d) || d > expand_radius_f) {
            alpha_values[i] = 0.0f;
            continue;
        }

        const float ratio = std::clamp(1.0f - (d / expand_radius_f), 0.0f, 1.0f);
        const float eased = bell_curve_weight(ratio);
        const float faded = std::pow(eased, kFadeExponent);
        alpha_values[i]   = base_alpha[i] * faded;
    }

    Uint32* dst_pixels = static_cast<Uint32*>(mask->pixels);
    for (int y = 0; y < expanded_h; ++y) {
        for (int x = 0; x < expanded_w; ++x) {
            const int idx = y * expanded_w + x;
            const Uint8 alpha = static_cast<Uint8>(std::clamp(alpha_values[idx], 0.0f, 255.0f));
            dst_pixels[idx] = SDL_MapRGBA(mask->format, 0, 0, 0, alpha);
        }
    }

    SDL_UnlockSurface(mask);
    SDL_UnlockSurface(src_rgba);
    SDL_FreeSurface(src_rgba);

    return mask;
}

void free_surface_list(std::vector<SDL_Surface*>& list) {
    for (SDL_Surface*& s : list) {
        if (s) {
            SDL_FreeSurface(s);
            s = nullptr;
        }
    }
    list.clear();
}

}  // namespace

std::pair<GenerateFadedMask::MaskVariants, bool> GenerateFadedMask::BuildMasks(
    const std::string& asset_name,
    const std::string& animation_id,
    const std::vector<int>& scale_steps,
    const MaskVariants& variant_frames)
{
    namespace fs = std::filesystem;
    using json = nlohmann::json;

    MaskVariants masks;
    masks.resize(variant_frames.size());

    const fs::path preferred_folder = fs::path("cache") / asset_name / "animations" / animation_id / "masks";
    const fs::path legacy_folder    = fs::path("cache") / asset_name / "animations" / animation_id / "mask";

    fs::path cache_folder = preferred_folder;
    fs::path meta_path    = cache_folder / "metadata.json";

    const std::size_t variant_count = variant_frames.size();
    const std::size_t frame_count   = variant_frames.empty() ? 0 : variant_frames.front().size();

    int reference_w = 0;
    int reference_h = 0;
    for (const auto& variant : variant_frames) {
        for (SDL_Surface* frame : variant) {
            if (frame) {
                reference_w = frame->w;
                reference_h = frame->h;
                break;
            }
        }
        if (reference_w > 0 && reference_h > 0) break;
    }

    const int expected_expand_radius = (reference_w > 0 && reference_h > 0)
        ? compute_expand_radius(reference_w, reference_h)
        : 0;
    const int expected_base_blur_radius = (expected_expand_radius > 0)
        ? compute_base_blur_radius(expected_expand_radius)
        : 0;

    json meta;
    bool cache_ok = false;
    bool metadata_loaded = CacheManager::load_metadata(meta_path.string(), meta);
    if (!metadata_loaded) {
        cache_folder = legacy_folder;
        meta_path    = cache_folder / "metadata.json";
        if (CacheManager::load_metadata(meta_path.string(), meta)) {
            metadata_loaded = true;
        } else {
            cache_folder = preferred_folder;
            meta_path    = cache_folder / "metadata.json";
        }
    }

    if (metadata_loaded) {
        try {
            const int stored_version       = meta.value("version", 0);
            const int stored_frame_count   = meta.value("frame_count", -1);
            const int stored_variant_count = meta.value("variant_count", -1);
            const float stored_ratio       = meta.value("expansion_ratio", kExpansionRatio);
            const int stored_expand_radius    = meta.value("expand_radius", expected_expand_radius);
            const int stored_base_blur_radius = meta.value("base_blur_radius", expected_base_blur_radius);

            bool steps_ok = false;
            if (meta.contains("scale_steps") && meta["scale_steps"].is_array()) {
                std::vector<int> stored_steps;
                stored_steps.reserve(meta["scale_steps"].size());
                bool valid = true;
                for (const auto& value : meta["scale_steps"]) {
                    if (!value.is_number_integer()) {
                        valid = false;
                        break;
                    }
                    stored_steps.push_back(value.get<int>());
                }
                if (valid) {
                    steps_ok = (stored_steps == scale_steps);
                }
            }

            const bool ratio_ok = std::fabs(stored_ratio - kExpansionRatio) <= 1e-4f;
            const bool blur_ok   = (expected_base_blur_radius == 0) || (stored_base_blur_radius == expected_base_blur_radius);
            const bool expand_ok = (expected_expand_radius == 0) || (stored_expand_radius == expected_expand_radius);

            cache_ok = (stored_version == kCacheVersion &&
                        stored_frame_count == static_cast<int>(frame_count) &&
                        stored_variant_count == static_cast<int>(variant_count) &&
                        ratio_ok && blur_ok && expand_ok && steps_ok);
        } catch (...) {
            cache_ok = false;
        }
    }

    if (cache_ok) {
        bool all_loaded = true;
        for (std::size_t variant_idx = 0; variant_idx < variant_count; ++variant_idx) {
            const std::string folder = variant_folder(cache_folder, variant_idx, scale_steps);
            std::vector<SDL_Surface*> loaded;
            if (!CacheManager::load_surface_sequence(folder, static_cast<int>(frame_count), loaded)) {
                all_loaded = false;
                free_surface_list(loaded);
                break;
            }
            masks[variant_idx] = std::move(loaded);
        }
        if (all_loaded) {
            return {std::move(masks), true};
        }
        for (auto& list : masks) {
            free_surface_list(list);
        }
        masks.assign(variant_count, {});
    }

    try {
        fs::create_directories(preferred_folder);
    } catch (...) {}

    for (std::size_t variant_idx = 0; variant_idx < variant_frames.size(); ++variant_idx) {
        const auto& frames = variant_frames[variant_idx];
        auto& mask_list    = masks[variant_idx];
        mask_list.reserve(frames.size());

        for (SDL_Surface* frame_surface : frames) {
            SDL_Surface* mask_surface = generate_mask_surface(frame_surface);
            if (!mask_surface) {
                // Clean up partial work and abort.
                for (auto& list : masks) {
                    free_surface_list(list);
                }
                return {MaskVariants{}, false};
            }
            mask_list.push_back(mask_surface);
        }

        const std::string folder = variant_folder(preferred_folder, variant_idx, scale_steps);
        CacheManager::save_surface_sequence(folder, mask_list);
    }

    json new_meta;
    new_meta["version"]        = kCacheVersion;
    new_meta["frame_count"]    = static_cast<int>(frame_count);
    new_meta["variant_count"]  = static_cast<int>(variant_count);
    new_meta["expansion_ratio"] = kExpansionRatio;
    new_meta["expand_radius"]     = expected_expand_radius;
    new_meta["base_blur_radius"]  = expected_base_blur_radius;

    nlohmann::json steps_json = nlohmann::json::array();
    for (int step : scale_steps) {
        steps_json.push_back(step);
    }
    new_meta["scale_steps"] = std::move(steps_json);

    if (reference_w > 0 && reference_h > 0) {
        new_meta["frame_width"]  = reference_w;
        new_meta["frame_height"] = reference_h;
    }

    CacheManager::save_metadata((preferred_folder / "metadata.json").string(), new_meta);

    return {std::move(masks), false};
}

std::vector<std::vector<SDL_Texture*>> GenerateFadedMask::SurfacesToTextures(
    SDL_Renderer* renderer,
    const MaskVariants& masks)
{
    std::vector<std::vector<SDL_Texture*>> textures;
    textures.resize(masks.size());
    for (std::size_t variant_idx = 0; variant_idx < masks.size(); ++variant_idx) {
        const auto& surfaces = masks[variant_idx];
        auto& out_list       = textures[variant_idx];
        out_list.reserve(surfaces.size());
        for (SDL_Surface* surface : surfaces) {
            SDL_Texture* texture = surface ? CacheManager::surface_to_texture(renderer, surface) : nullptr;
            out_list.push_back(texture);
        }
    }
    return textures;
}
