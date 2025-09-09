#include "Vibble_controller.hpp"

#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "core/active_assets_manager.hpp"
#include "utils/input.hpp"
#include "utils/area.hpp"
#include "asset/move.hpp"

#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>

VibbleController::VibbleController(Assets* assets, Asset* player, ActiveAssetsManager& aam)
    : assets_(assets), player_(player), aam_(aam), anim_(player, aam, true) {}

int VibbleController::get_dx() const { return dx_; }
int VibbleController::get_dy() const { return dy_; }

bool VibbleController::aabb(const Area& A, const Area& B) const {
    auto [a_minx, a_miny, a_maxx, a_maxy] = A.get_bounds();
    auto [b_minx, b_miny, b_maxx, b_maxy] = B.get_bounds();
    return !(a_maxx < b_minx || b_maxx < a_minx ||
             a_maxy < b_miny || b_maxy < a_miny);
}

bool VibbleController::pointInAABB(SDL_Point p, const Area& B) const {
    auto [b_minx, b_miny, b_maxx, b_maxy] = B.get_bounds();
    return (p.x >= b_minx && p.x <= b_maxx && p.y >= b_miny && p.y <= b_maxy);
}

bool VibbleController::canMove(int, int) {
    // Placeholder collision check
    return true;
}

void VibbleController::interaction() {
    // Placeholder for interaction logic
}

void VibbleController::movement(const Input& input) {
    dx_ = dy_ = 0;
    if (!player_) return;

    bool up    = input.isScancodeDown(SDL_SCANCODE_W);
    bool down  = input.isScancodeDown(SDL_SCANCODE_S);
    bool left  = input.isScancodeDown(SDL_SCANCODE_A);
    bool right = input.isScancodeDown(SDL_SCANCODE_D);

    int move_x = (right ? 1 : 0) - (left ? 1 : 0);
    int move_y = (down  ? 1 : 0) - (up    ? 1 : 0);

    const std::string current = player_->get_current_animation();
    if (move_x == 0 && move_y == 0) {
        if (current != "default" && player_->next_animation.empty())
            player_->change_animation_qued("default");
        return;
    }

    if (!(move_x != 0 && move_y != 0)) {
        std::string anim;
        if      (move_y < 0) anim = "backward";
        else if (move_y > 0) anim = "forward";
        else if (move_x < 0) anim = "left";
        else if (move_x > 0) anim = "right";
        if (!anim.empty() && anim != current && player_->next_animation.empty())
            player_->change_animation_qued(anim);
    }
}

void VibbleController::handle_teleport(const Input&) {
    // Teleport logic disabled in this minimal implementation
}

void VibbleController::update(const Input& input) {
    dx_ = dy_ = 0;
    movement(input);
    interaction();
    anim_.update();
}

