#pragma once

#include <SDL.h>

class RelativeRoomPosition {
public:
    RelativeRoomPosition(SDL_Point offset = SDL_Point{0, 0},
                         int original_width = 0,
                         int original_height = 0);

    SDL_Point original_offset() const { return offset_; }
    int original_width() const { return original_width_; }
    int original_height() const { return original_height_; }

    SDL_Point scaled_offset(int current_width, int current_height) const;

    SDL_Point resolve(SDL_Point room_center, int current_width, int current_height) const;

    SDL_Point to_original(SDL_Point scaled_offset, int current_width, int current_height) const;

    static SDL_Point ScaleOffset(SDL_Point offset, int original_width, int original_height, int current_width, int current_height);

    static SDL_Point Resolve(SDL_Point room_center, SDL_Point offset, int original_width, int original_height, int current_width, int current_height);

    static SDL_Point ToOriginal(SDL_Point scaled_offset, int original_width, int original_height, int current_width, int current_height);

private:
    SDL_Point offset_{};
    int original_width_{};
    int original_height_{};
};
