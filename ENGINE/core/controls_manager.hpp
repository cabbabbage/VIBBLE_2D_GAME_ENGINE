

#pragma once

#include <unordered_set>
#include <SDL2/SDL.h>
#include "asset/asset.hpp"
#include "utils/area.hpp"
#include "active_assets_manager.hpp"

class Input;
class Assets; // fwd

class ControlsManager {
public:
    ControlsManager(Assets* assets, Asset* player, ActiveAssetsManager& aam);

    void update(const Input& input);
    void movement(const Input& input);
    void interaction();
    void handle_teleport(const Input& input);
    bool canMove(int offset_x, int offset_y);

    int get_dx() const;
    int get_dy() const;

private:
    bool aabb(const Area& A, const Area& B) const;
    bool pointInAABB(int x, int y, const Area& B) const;

    Assets* assets_ = nullptr;
    Asset* player_;
    ActiveAssetsManager& aam_;

    int dx_;
    int dy_;

    SDL_Point teleport_point_;
    bool teleport_set_;

    // Marker spawned on SPACE; removed on 'q'
    Asset* marker_asset_ = nullptr;
};
