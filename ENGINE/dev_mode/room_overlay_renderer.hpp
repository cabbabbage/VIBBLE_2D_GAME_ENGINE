#pragma once

#include <SDL.h>
class Area;
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
    const Area& area,
    const RoomBoundsOverlayStyle& style);

}

