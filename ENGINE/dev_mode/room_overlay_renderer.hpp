#pragma once

#include <SDL.h>
#include <tuple>

class camera;

namespace dm_draw {

struct RoomBoundsOverlayStyle {
    SDL_Color outline{};
    SDL_Color fill{};
    SDL_Color center{};
};

const RoomBoundsOverlayStyle& ResolveRoomBoundsOverlayStyle();

void RenderRoomBoundsOverlay(
    SDL_Renderer* renderer,
    const camera& cam,
    const std::tuple<int, int, int, int>& bounds,
    SDL_Point center,
    const RoomBoundsOverlayStyle& style);

}

