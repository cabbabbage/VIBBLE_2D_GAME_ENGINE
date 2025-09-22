#include "Vibble_controller.hpp"

#include "asset/Asset.hpp"
#include "utils/input.hpp"

#include <cmath>
#include <string>

VibbleController::VibbleController(Asset* player)
    : player_(player) {}

int VibbleController::get_dx() const { return dx_; }
int VibbleController::get_dy() const { return dy_; }

void VibbleController::movement(const Input& input) {
    dx_ = dy_ = 0;
    if (!player_) return;

    const bool up    = input.isScancodeDown(SDL_SCANCODE_W);
    const bool down  = input.isScancodeDown(SDL_SCANCODE_S);
    const bool left  = input.isScancodeDown(SDL_SCANCODE_A);
    const bool right = input.isScancodeDown(SDL_SCANCODE_D);

    const int raw_x = (right ? 1 : 0) - (left ? 1 : 0);
    const int raw_y = (down  ? 1 : 0) - (up    ? 1 : 0);

    auto send_to_animation = [&](int mx, int my) {
        if (player_->anim_) {
            player_->anim_->move(mx, my);
        }
    };

    if (raw_x == 0 && raw_y == 0) {
        send_to_animation(0, 0);
        return;
    }

    const double length = std::sqrt(static_cast<double>(raw_x * raw_x + raw_y * raw_y));
    if (length <= 0.0) {
        send_to_animation(0, 0);
        return;
    }

    const double scale = 5.0 / length;
    int move_x = static_cast<int>(std::lround(static_cast<double>(raw_x) * scale));
    int move_y = static_cast<int>(std::lround(static_cast<double>(raw_y) * scale));

    auto ensure_non_zero = [](int component, int raw) {
        if (component == 0 && raw != 0) {
            return (raw > 0) ? 1 : -1;
        }
        return component;
    };

    move_x = ensure_non_zero(move_x, raw_x);
    move_y = ensure_non_zero(move_y, raw_y);

    auto magnitude_sq = [&]() {
        return move_x * move_x + move_y * move_y;
    };

    auto reduce_once = [](int value) {
        if (value > 0) return value - 1;
        if (value < 0) return value + 1;
        return value;
    };

    int mag_sq = magnitude_sq();
    if (mag_sq == 0) {
        move_x = ensure_non_zero(move_x, raw_x);
        move_y = ensure_non_zero(move_y, raw_y);
        mag_sq = magnitude_sq();
    }

    while (mag_sq > 25) {
        if (std::abs(move_x) >= std::abs(move_y)) {
            move_x = reduce_once(move_x);
        } else {
            move_y = reduce_once(move_y);
        }
        mag_sq = magnitude_sq();
    }

    while (mag_sq < 25 && (move_x != 0 || move_y != 0)) {
        bool adjusted = false;
        if (std::abs(move_x) <= std::abs(move_y) && move_x != 0) {
            const int step = (move_x > 0) ? 1 : -1;
            const int candidate = move_x + step;
            const int candidate_mag = candidate * candidate + move_y * move_y;
            if (candidate_mag <= 25) {
                move_x = candidate;
                mag_sq = candidate_mag;
                adjusted = true;
            }
        }
        if (!adjusted && move_y != 0) {
            const int step = (move_y > 0) ? 1 : -1;
            const int candidate = move_y + step;
            const int candidate_mag = move_x * move_x + candidate * candidate;
            if (candidate_mag <= 25) {
                move_y = candidate;
                mag_sq = candidate_mag;
                adjusted = true;
            }
        }
        if (!adjusted) break;
    }

    dx_ = move_x;
    dy_ = move_y;
    send_to_animation(dx_, dy_);
}

void VibbleController::update(const Input& input) {
    dx_ = dy_ = 0;

    // Decide movement direction + possibly force an animation switch
    movement(input);
}
