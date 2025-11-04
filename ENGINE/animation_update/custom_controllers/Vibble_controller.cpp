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

    if(dash && canDash == true) {
        Dash();

    }

    float speedMultiplier = kWalkSpeed;
    if(isDashing) {
        speedMultiplier *= dashingPower;
    }

    dx_ = raw_x * speedMultiplier * stride_count;
    dy_ = raw_y * speedMultiplier * stride_count;

    const std::string animation_id = animation_for_direction(raw_x, raw_y);

    player_->anim_->move(SDL_Point{ dx_, dy_ }, animation_id);

}

void VibbleController::update(const Input& input) {
    using namespace std::chrono;
    auto now = steady_clock::now();

    if(isDashing && now >= dashEndTime) {
        isDashing = false;
        cooldownEndTime = now + duration_cast<steady_clock::duration>(duration<float>(dashingCooldown));
    }

    if(!canDash && !isDashing && now >= cooldownEndTime) {
        canDash = true;
    }

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
    if(!canDash) return;
    // Starting our dash
    canDash = false;
    isDashing = true;
    dashEndTime = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<float>(dashingTime)
    ); // Ending dash within update and setting the bools back
    };

