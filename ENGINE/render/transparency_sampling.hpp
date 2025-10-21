#pragma once

#include <SDL.h>

#include <cstdint>
#include <string>

namespace vibble::render {

struct TransparencySampleResult {
    bool        success        = false;
    float       average        = 0.0f;
    float       min            = 0.0f;
    float       max            = 0.0f;
    std::string error_message;
};

struct TransparencyReadbackStats {
    std::uint64_t attempts             = 0;
    std::uint64_t successes            = 0;
    std::uint64_t failures             = 0;
    std::uint64_t consecutive_failures = 0;
};

TransparencySampleResult sample_texture_transparency(SDL_Renderer* renderer, SDL_Texture* texture);
TransparencyReadbackStats transparency_readback_stats();
void reset_transparency_readback_stats();

}  // namespace vibble::render

