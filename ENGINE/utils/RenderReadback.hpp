#pragma once

#include <SDL.h>

#include <cstdint>
#include <vector>

namespace lighting {

struct MeasureResult {
    enum class Status {
        Success,
        InvalidInput,
        AllocationFailed,
        ReadbackFailed
    };

    Status status = Status::InvalidInput;
    int    width  = 0;
    int    height = 0;
    std::vector<std::uint8_t> pixels;

    bool success() const { return status == Status::Success; }
};

MeasureResult readRgba(SDL_Renderer* renderer, SDL_Texture* texture);

} // namespace lighting

