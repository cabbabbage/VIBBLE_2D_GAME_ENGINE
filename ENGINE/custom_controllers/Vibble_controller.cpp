#include "Vibble_controller.hpp"

#include "asset/Asset.hpp"
#include "custom_controllers/controller_path_utils.hpp"
#include "utils/input.hpp"

#include <cmath>
#include <string>
#include <vector>

namespace {

SDL_Point normalized_step(int raw_x, int raw_y, int target_speed) {
    if (target_speed <= 0) {
        return SDL_Point{ 0, 0 };
    }

    const int magnitude_sq = raw_x * raw_x + raw_y * raw_y;
    if (magnitude_sq == 0) {
        return SDL_Point{ 0, 0 };
    }

    const double length = std::sqrt(static_cast<double>(magnitude_sq));
    if (length <= 0.0) {
        return SDL_Point{ 0, 0 };
    }

    const double scale = static_cast<double>(target_speed) / length;
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

    auto magnitude_squared = [&]() {
        return move_x * move_x + move_y * move_y;
    };

    auto reduce_once = [](int value) {
        if (value > 0) return value - 1;
        if (value < 0) return value + 1;
        return value;
    };

    const int target_magnitude_sq = target_speed * target_speed;

    int adjusted_mag_sq = magnitude_squared();
    if (adjusted_mag_sq == 0) {
        move_x = ensure_non_zero(move_x, raw_x);
        move_y = ensure_non_zero(move_y, raw_y);
        adjusted_mag_sq = magnitude_squared();
    }

    while (adjusted_mag_sq > target_magnitude_sq) {
        if (std::abs(move_x) >= std::abs(move_y)) {
            move_x = reduce_once(move_x);
        } else {
            move_y = reduce_once(move_y);
        }
        adjusted_mag_sq = magnitude_squared();
    }

    while (adjusted_mag_sq < target_magnitude_sq && (move_x != 0 || move_y != 0)) {
        bool adjusted = false;
        if (std::abs(move_x) <= std::abs(move_y) && move_x != 0) {
            const int step = (move_x > 0) ? 1 : -1;
            const int candidate = move_x + step;
            const int candidate_mag_sq = candidate * candidate + move_y * move_y;
            if (candidate_mag_sq <= target_magnitude_sq) {
                move_x = candidate;
                adjusted_mag_sq = candidate_mag_sq;
                adjusted = true;
            }
        }
        if (!adjusted && move_y != 0) {
            const int step = (move_y > 0) ? 1 : -1;
            const int candidate = move_y + step;
            const int candidate_mag_sq = move_x * move_x + candidate * candidate;
            if (candidate_mag_sq <= target_magnitude_sq) {
                move_y = candidate;
                adjusted_mag_sq = candidate_mag_sq;
                adjusted = true;
            }
        }
        if (!adjusted) {
            break;
        }
    }

    return SDL_Point{ move_x, move_y };
}

} // namespace

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
    const bool sprint = input.isScancodeDown(SDL_SCANCODE_LSHIFT) ||
                        input.isScancodeDown(SDL_SCANCODE_RSHIFT);

    const int raw_x = (right ? 1 : 0) - (left ? 1 : 0);
    const int raw_y = (down  ? 1 : 0) - (up    ? 1 : 0);

    if (raw_x == 0 && raw_y == 0) {
        if (player_ && player_->anim_) {
            player_->anim_->move({}, 0);
        }
        return;
    }

    SDL_Point step_delta = normalized_step(raw_x, raw_y, kWalkSpeed);
    if (step_delta.x == 0 && step_delta.y == 0) {
        if (player_ && player_->anim_) {
            player_->anim_->move({}, 0);
        }
        return;
    }

    const int radius = controller_paths::neighbor_radius(player_);
    if (radius > 0) {
        step_delta = controller_paths::clamp_delta(step_delta, radius);
    }

    if (step_delta.x == 0 && step_delta.y == 0) {
        if (player_ && player_->anim_) {
            player_->anim_->move({}, 0);
        }
        return;
    }

    const int stride_count = sprint ? kSprintMultiplier : 1;

    std::vector<SDL_Point> path;
    path.reserve(static_cast<std::size_t>(stride_count));
    for (int i = 0; i < stride_count; ++i) {
        path.push_back(step_delta);
    }

    dx_ = step_delta.x * stride_count;
    dy_ = step_delta.y * stride_count;

    if (player_ && player_->anim_) {
        player_->anim_->move(path, 0);
    }
}

void VibbleController::update(const Input& input) {
    dx_ = dy_ = 0;

    movement(input);
}
