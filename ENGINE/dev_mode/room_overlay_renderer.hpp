#pragma once

#include <SDL.h>
class Area;
class camera_grid;

namespace dm_draw {

struct RoomBoundsOverlayStyle {
    SDL_Color outline{};
    SDL_Color fill{};
    SDL_Color center{};
};

RoomBoundsOverlayStyle ResolveRoomBoundsOverlayStyle(SDL_Color base_color);

void RenderRoomBoundsOverlay( SDL_Renderer* renderer, const camera_grid& cam, const Area& area, const RoomBoundsOverlayStyle& style);

}

