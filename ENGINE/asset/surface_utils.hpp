
#pragma once

#include <SDL.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace asset {
namespace surface_utils {

inline constexpr std::uint64_t kSignatureOffset = 1469598103934665603ull;
inline constexpr std::uint64_t kSignaturePrime  = 1099511628211ull;

SDL_Surface* duplicate_surface(SDL_Surface* surface);

std::uint64_t mix_signature(std::uint64_t seed, std::uint64_t value);

std::uint64_t hash_surface_pixels(SDL_Surface* surface, std::uint64_t seed);

std::uint64_t compute_surface_signature(const std::vector<std::vector<SDL_Surface*>>& variants);

}
}
