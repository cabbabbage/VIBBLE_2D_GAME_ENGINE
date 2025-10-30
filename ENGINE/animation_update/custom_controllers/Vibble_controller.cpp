#include "Vibble_controller.hpp"

#include "animation_update/animation_update.hpp"
#include "asset/Asset.hpp"
#include "utils/input.hpp"

VibbleController::VibbleController(Asset* player)
    : player_(player) {}

int VibbleController::get_dx() const { return dx_; }
int VibbleController::get_dy() const { return dy_; }

void VibbleController::movement(const Input& input) {
    dx_ = dy_ = 0;
    if (!player_ || !player_->anim_) return;

    const bool up    = input.isScancodeDown(SDL_SCANCODE_W) || input.isScancodeDown(SDL_SCANCODE_UP);
    const bool down  = input.isScancodeDown(SDL_SCANCODE_S) || input.isScancodeDown(SDL_SCANCODE_DOWN);
    const bool left  = input.isScancodeDown(SDL_SCANCODE_A) || input.isScancodeDown(SDL_SCANCODE_LEFT);
    const bool right = input.isScancodeDown(SDL_SCANCODE_D) || input.isScancodeDown(SDL_SCANCODE_RIGHT);
    const bool sprint = input.isScancodeDown(SDL_SCANCODE_LSHIFT) || input.isScancodeDown(SDL_SCANCODE_RSHIFT);
    const bool dash = input.isScancodeDown(SDL_SCANCODE_SPACE);

    const int raw_x = (right ? 1 : 0) - (left ? 1 : 0);
    const int raw_y = (down  ? 1 : 0) - (up    ? 1 : 0);

    if (raw_x == 0 && raw_y == 0) {
        player_->anim_->move(SDL_Point{ 0, 0 }, animation_update::detail::kDefaultAnimation);
        return;
    }

    const int stride_count = sprint ? kSprintMultiplier : 1;

    if(dash) Dash();

    dx_ = raw_x * kWalkSpeed * stride_count;
    dy_ = raw_y * kWalkSpeed * stride_count;

    const std::string animation_id = animation_for_direction(raw_x, raw_y);

    player_->anim_->move(SDL_Point{ dx_, dy_ }, animation_id);

}

void VibbleController::update(const Input& input) {
    dx_ = dy_ = 0;

    movement(input);
}

std::string VibbleController::animation_for_direction(int raw_x, int raw_y) const {
    const int sign_x = (raw_x > 0) - (raw_x < 0);
    const int sign_y = (raw_y > 0) - (raw_y < 0);

    if (sign_x == 0 && sign_y == 0) {
        return std::string{ animation_update::detail::kDefaultAnimation };
    }

    if (!player_ || !player_->info) {
        return std::string{ animation_update::detail::kDefaultAnimation };
    }

    const auto& animations = player_->info->animations;

    auto has_animation = [&animations](const std::string& name) {
        return animations.find(name) != animations.end();
    };

    const std::string forward_anim   = "forward";
    const std::string backward_anim  = "backward";
    const std::string left_anim      = "left";
    const std::string right_anim     = "right";
    const std::string dash_anim      = "dash";

    if (sign_x != 0 && sign_y != 0) {
        const std::string vertical_choice = (sign_y < 0) ? backward_anim : forward_anim;
        if (has_animation(vertical_choice)) {
            return vertical_choice;
        }

        const std::string horizontal_choice = (sign_x < 0) ? left_anim : right_anim;
        if (has_animation(horizontal_choice)) {
            return horizontal_choice;
        }
    }

    if (sign_y != 0) {
        const std::string vertical_choice = (sign_y < 0) ? backward_anim : forward_anim;
        if (has_animation(vertical_choice)) {
            return vertical_choice;
        }
    }

    if (sign_x != 0) {
        const std::string horizontal_choice = (sign_x < 0) ? left_anim : right_anim;
        if (has_animation(horizontal_choice)) {
            return horizontal_choice;
        }
    }

    if (has_animation(animation_update::detail::kDefaultAnimation)) {
        return std::string{ animation_update::detail::kDefaultAnimation };
    }

    return std::string{ animation_update::detail::kDefaultAnimation };
}

void VibbleController::Dash() {
    canDash = false;
    isDashing = true;
    /**
     * Set gravity to 0 theres gravity, 
     * Set velocity(transform.x * dashingPower, 0f);
     * Keep dashing for dashingTime: waitForSeconds(dashingTime)
     * Set gravity back to original
     */
    if(get_dx() > 0) { }
    int temp_speed = kWalkSpeed;
    kWalkSpeed = kWalkSpeed * dashingPower;
    

    isDashing = false;
    /**
     * WaitForSeconds(dashingCooldown)
     */
    canDash = true;
    };

