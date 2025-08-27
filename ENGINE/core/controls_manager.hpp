

#pragma once

#include <unordered_set>
#include <SDL2/SDL.h>
#include "asset/asset.hpp"
#include "utils/area.hpp"
#include "active_assets_manager.hpp"

class ControlsManager {
public:
    ControlsManager(Asset* player, ActiveAssetsManager& aam);

    void update(const std::unordered_set<SDL_Keycode>& keys);
    void movement(const std::unordered_set<SDL_Keycode>& keys);
    void interaction();
    void handle_teleport(const std::unordered_set<SDL_Keycode>& keys);
    bool canMove(int offset_x, int offset_y);

    int get_dx() const;
    int get_dy() const;

private:
    bool aabb(const Area& A, const Area& B) const;
    bool pointInAABB(int x, int y, const Area& B) const;

    Asset* player_;
    ActiveAssetsManager& aam_;

    int dx_;
    int dy_;

    SDL_Point teleport_point_;
    bool teleport_set_;
};
