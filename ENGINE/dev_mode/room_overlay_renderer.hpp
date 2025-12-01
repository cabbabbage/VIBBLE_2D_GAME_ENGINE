#pragma once

#include <SDL.h>
class Area;
class WarpedScreenGrid;

namespace dm_draw {

struct RoomBoundsOverlayStyle {
    SDL_Color outline{};
    SDL_Color fill{};
    SDL_Color center{};
};

RoomBoundsOverlayStyle ResolveRoomBoundsOverlayStyle(SDL_Color base_color);

void RenderRoomBoundsOverlay( SDL_Renderer* renderer, const WarpedScreenGrid& cam, const Area& area, const RoomBoundsOverlayStyle& style);

}

