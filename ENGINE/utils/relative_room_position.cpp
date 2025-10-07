#include "relative_room_position.hpp"

#include <cmath>

namespace {
int sanitize_dimension(int value, int fallback) {
    if (value > 0) return value;
    if (fallback > 0) return fallback;
    return 1;
}
}

RelativeRoomPosition::RelativeRoomPosition(SDL_Point offset,
                                           int original_width,
                                           int original_height)
    : offset_(offset),
      original_width_(original_width),
      original_height_(original_height) {}

SDL_Point RelativeRoomPosition::scaled_offset(int current_width, int current_height) const {
    const int base_w = sanitize_dimension(original_width_, current_width);
    const int base_h = sanitize_dimension(original_height_, current_height);
    const int curr_w = sanitize_dimension(current_width, base_w);
    const int curr_h = sanitize_dimension(current_height, base_h);

    const double rx = static_cast<double>(curr_w) / static_cast<double>(base_w);
    const double ry = static_cast<double>(curr_h) / static_cast<double>(base_h);

    SDL_Point result{0, 0};
    result.x = static_cast<int>(std::lround(static_cast<double>(offset_.x) * rx));
    result.y = static_cast<int>(std::lround(static_cast<double>(offset_.y) * ry));
    return result;
}

SDL_Point RelativeRoomPosition::resolve(SDL_Point room_center, int current_width, int current_height) const {
    SDL_Point scaled = scaled_offset(current_width, current_height);
    return SDL_Point{ room_center.x + scaled.x, room_center.y + scaled.y };
}

SDL_Point RelativeRoomPosition::to_original(SDL_Point scaled_offset, int current_width, int current_height) const {
    const int base_w = sanitize_dimension(original_width_, current_width);
    const int base_h = sanitize_dimension(original_height_, current_height);
    const int curr_w = sanitize_dimension(current_width, base_w);
    const int curr_h = sanitize_dimension(current_height, base_h);

    const double rx = static_cast<double>(base_w) / static_cast<double>(curr_w);
    const double ry = static_cast<double>(base_h) / static_cast<double>(curr_h);

    SDL_Point result{0, 0};
    result.x = static_cast<int>(std::lround(static_cast<double>(scaled_offset.x) * rx));
    result.y = static_cast<int>(std::lround(static_cast<double>(scaled_offset.y) * ry));
    return result;
}

SDL_Point RelativeRoomPosition::ScaleOffset(SDL_Point offset,
                                            int original_width,
                                            int original_height,
                                            int current_width,
                                            int current_height) {
    RelativeRoomPosition rel(offset, original_width, original_height);
    return rel.scaled_offset(current_width, current_height);
}

SDL_Point RelativeRoomPosition::Resolve(SDL_Point room_center,
                                        SDL_Point offset,
                                        int original_width,
                                        int original_height,
                                        int current_width,
                                        int current_height) {
    RelativeRoomPosition rel(offset, original_width, original_height);
    return rel.resolve(room_center, current_width, current_height);
}

SDL_Point RelativeRoomPosition::ToOriginal(SDL_Point scaled_offset,
                                           int original_width,
                                           int original_height,
                                           int current_width,
                                           int current_height) {
    RelativeRoomPosition rel(SDL_Point{0, 0}, original_width, original_height);
    return rel.to_original(scaled_offset, current_width, current_height);
}
