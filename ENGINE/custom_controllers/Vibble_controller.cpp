#include "Vibble_controller.hpp"

#include "asset/Asset.hpp"
#include "utils/input.hpp"

#include <string>

VibbleController::VibbleController(Asset* player, ActiveAssetsManager& aam)
    : player_(player), anim_(player, aam, true) {}

int VibbleController::get_dx() const { return dx_; }
int VibbleController::get_dy() const { return dy_; }

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
        // idle → default
        if (current != "default") {
            if (player_->info && player_->info->animations.count("default")) {
                anim_.set_animation_now("default");
            }
        }
        return;
    }

    if (!(move_x != 0 && move_y != 0)) {
        std::string anim;
        if      (move_y < 0) anim = "backward";
        else if (move_y > 0) anim = "forward";
        else if (move_x < 0) anim = "left";
        else if (move_x > 0) anim = "right";

        if (!anim.empty() && anim != current) {
            if (player_->info && player_->info->animations.count(anim)) {
                anim_.set_animation_now(anim);
            }
        }
    }
}

void VibbleController::update(const Input& input) {
    dx_ = dy_ = 0;

    // Decide movement direction + possibly force an animation switch
    movement(input);

    anim_.update();
}
