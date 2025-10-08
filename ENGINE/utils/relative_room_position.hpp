#pragma once

#include <SDL.h>

// Utility to translate stored room-relative offsets into scaled positions.
//
// The class keeps track of an offset from the room center that was computed
// against an original room size. It can then resolve that offset for a room
// that may have been resized, ensuring that placement remains consistent.
class RelativeRoomPosition {
public:
    RelativeRoomPosition(SDL_Point offset = SDL_Point{0, 0},
                         int original_width = 0,
                         int original_height = 0);

    SDL_Point original_offset() const { return offset_; }
    int original_width() const { return original_width_; }
    int original_height() const { return original_height_; }

    // Compute the scaled offset for the supplied room size.
    SDL_Point scaled_offset(int current_width, int current_height) const;

    // Compute the absolute position using the supplied room center and size.
    SDL_Point resolve(SDL_Point room_center, int current_width, int current_height) const;

    // Convert a scaled offset back into the stored/original coordinate space.
    SDL_Point to_original(SDL_Point scaled_offset, int current_width, int current_height) const;

    static SDL_Point ScaleOffset(SDL_Point offset,
                                 int original_width,
                                 int original_height,
                                 int current_width,
                                 int current_height);

    static SDL_Point Resolve(SDL_Point room_center,
                              SDL_Point offset,
                              int original_width,
                              int original_height,
                              int current_width,
                              int current_height);

    static SDL_Point ToOriginal(SDL_Point scaled_offset,
                                int original_width,
                                int original_height,
                                int current_width,
                                int current_height);

private:
    SDL_Point offset_{};
    int original_width_{};
    int original_height_{};
};
