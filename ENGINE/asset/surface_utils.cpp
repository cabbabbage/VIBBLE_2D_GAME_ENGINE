#include "asset/surface_utils.hpp"

namespace asset {
namespace surface_utils {

std::uint64_t mix_signature(std::uint64_t seed, std::uint64_t value) {
    seed ^= value;
    seed *= kSignaturePrime;
    return seed;
}

SDL_Surface* duplicate_surface(SDL_Surface* surface) {
    if (!surface) {
        return nullptr;
    }
    SDL_Surface* copy = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA8888, 0);
    if (!copy) {
        SDL_Surface* fallback =
            SDL_CreateRGBSurfaceWithFormat(0, surface->w, surface->h, 32, SDL_PIXELFORMAT_RGBA8888);
        if (!fallback) {
            return nullptr;
        }
        SDL_Rect rect{0, 0, surface->w, surface->h};
        if (SDL_BlitSurface(surface, &rect, fallback, &rect) != 0) {
            SDL_FreeSurface(fallback);
            return nullptr;
        }
        copy = fallback;
    }
    return copy;
}

std::uint64_t hash_surface_pixels(SDL_Surface* surface, std::uint64_t seed) {
    if (!surface || !surface->pixels) {
        return mix_signature(seed, 0);
    }
    seed = mix_signature(seed, static_cast<std::uint64_t>(surface->w));
    seed = mix_signature(seed, static_cast<std::uint64_t>(surface->h));
    seed = mix_signature(seed, static_cast<std::uint64_t>(surface->pitch));
    const std::size_t bytes =
        static_cast<std::size_t>(surface->pitch) * static_cast<std::size_t>(surface->h);
    const std::uint8_t* data = static_cast<std::uint8_t*>(surface->pixels);
    for (std::size_t idx = 0; idx < bytes; ++idx) {
        seed ^= static_cast<std::uint64_t>(data[idx]);
        seed *= kSignaturePrime;
    }
    return seed;
}

std::uint64_t compute_surface_signature(const std::vector<std::vector<SDL_Surface*>>& variants) {
    std::uint64_t signature = kSignatureOffset;
    for (std::size_t variant_idx = 0; variant_idx < variants.size(); ++variant_idx) {
        signature = mix_signature(signature, static_cast<std::uint64_t>(variant_idx));
        const auto& stack = variants[variant_idx];
        signature = mix_signature(signature, static_cast<std::uint64_t>(stack.size()));
        for (std::size_t frame_idx = 0; frame_idx < stack.size(); ++frame_idx) {
            signature = mix_signature(signature, static_cast<std::uint64_t>(frame_idx));
            signature = hash_surface_pixels(stack[frame_idx], signature);
        }
    }
    return signature;
}

}
}

