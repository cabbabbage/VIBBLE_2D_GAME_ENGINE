#include "path_sanitizer.hpp"

#include "asset/Asset.hpp"

std::vector<SDL_Point> PathSanitizer::sanitize(const Asset&,
                                               const std::vector<SDL_Point>& absolute_checkpoints) const {
    return absolute_checkpoints;
}