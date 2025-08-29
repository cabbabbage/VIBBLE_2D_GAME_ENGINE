
#include "controls_manager.hpp"
#include "active_assets_manager.hpp"
#include "core/Assets.hpp"
#include "utils/input.hpp"
#include <cmath>
#include <iostream>
#include <random>


bool ControlsManager::aabb(const Area& A, const Area& B) const {
    auto [a_minx, a_miny, a_maxx, a_maxy] = A.get_bounds();
    auto [b_minx, b_miny, b_maxx, b_maxy] = B.get_bounds();
    return !(a_maxx < b_minx || b_maxx < a_minx ||
             a_maxy < b_miny || b_maxy < a_miny);
}


bool ControlsManager::pointInAABB(int x, int y, const Area& B) const {
    auto [b_minx, b_miny, b_maxx, b_maxy] = B.get_bounds();
    return (x >= b_minx && x <= b_maxx &&
            y >= b_miny && y <= b_maxy);
}


ControlsManager::ControlsManager(Assets* assets, Asset* player, ActiveAssetsManager& aam)
    : assets_(assets),
      player_(player),
      aam_(aam),
      dx_(0),
      dy_(0),
      teleport_set_(false)
{}


void ControlsManager::movement(const Input& input) {
    dx_ = dy_ = 0;
    if (!player_) return;

    bool up    = input.isKeyDown(SDLK_w);
    bool down  = input.isKeyDown(SDLK_s);
    bool left  = input.isKeyDown(SDLK_a);
    bool right = input.isKeyDown(SDLK_d);

    int move_x = (right ? 1 : 0) - (left ? 1 : 0);
    int move_y = (down  ? 1 : 0) - (up    ? 1 : 0);

    bool any_movement = (move_x != 0 || move_y != 0);
    bool diagonal     = (move_x != 0 && move_y != 0);
    const std::string current = player_->get_current_animation();

    if (any_movement) {
        float len = std::sqrt(float(move_x * move_x + move_y * move_y));
        if (len == 0.0f) return;

        float base_speed = player_->player_speed;
        if (input.isKeyDown(SDLK_LSHIFT) || input.isKeyDown(SDLK_RSHIFT)) {
            base_speed *= 1.5f;  
        }

        float speed = base_speed / len;
        int ox = static_cast<int>(std::round(move_x * speed));
        int oy = static_cast<int>(std::round(move_y * speed));

        if (canMove(ox, oy)) {
            dx_ = ox;
            dy_ = oy;
            player_->set_position(player_->pos_X + dx_,
                                  player_->pos_Y + dy_);
        }

        if (!diagonal) {
            std::string anim;
            if      (move_y < 0) anim = "backward";
            else if (move_y > 0) anim = "forward";
            else if (move_x < 0) anim = "left";
            else if (move_x > 0) anim = "right";

            if (!anim.empty() && anim != current)
                player_->change_animation(anim);
        }
    }
    else {
        if (current != "default")
            player_->change_animation("default");
    }
}


bool ControlsManager::canMove(int offset_x, int offset_y) {
    if (!player_) return false;

    int test_x = player_->pos_X + offset_x;
    int test_y = player_->pos_Y + offset_y - player_->info->z_threshold;

    for (Asset* a : aam_.getImpassableClosest()) {
        if (!a || a == player_) continue;
        Area obstacle = a->get_area("passability");
        if (obstacle.contains_point({ test_x, test_y })) {
            return false;
        }
    }
    return true;
}


void ControlsManager::interaction() {
    if (!player_ || !player_->info) {
        return;
    }

    int px = player_->pos_X;
    int py = player_->pos_Y - player_->info->z_threshold;

    for (Asset* a : aam_.getInteractiveClosest()) {
        if (!a || a == player_) continue;
        Area ia = a->get_area("interaction");
        if (pointInAABB(px, py, ia)) {
            a->change_animation("interaction");
        }
    }
}


void ControlsManager::handle_teleport(const Input& input) {
    if (!player_) return;

    // On press: set teleport point and drop a marker nearby (~30px radius)
    if (input.wasKeyPressed(SDLK_SPACE)) {
        teleport_point_ = { player_->pos_X, player_->pos_Y };
        teleport_set_ = true;

        // Remove existing marker if present
        if (marker_asset_ && assets_) {
            assets_->remove(marker_asset_);
            marker_asset_ = nullptr;
        }

        // Spawn new marker near the player
        if (assets_) {
            static std::mt19937 rng{ std::random_device{}() };
            std::uniform_real_distribution<float> angle(0.0f, 6.2831853f);
            float a = angle(rng);
            int r = 30; // radius in pixels
            int mx = player_->pos_X + static_cast<int>(std::round(std::cos(a) * r));
            int my = player_->pos_Y + static_cast<int>(std::round(std::sin(a) * r));
            marker_asset_ = assets_->spawn_asset("marker", mx, my);
        }
    }

    // On press: teleport and remove marker
    if (input.wasKeyPressed(SDLK_q) && teleport_set_) {
        player_->set_position(teleport_point_.x, teleport_point_.y);
        teleport_point_ = { 0, 0 };
        teleport_set_ = false;

        if (marker_asset_ && assets_) {
            assets_->remove(marker_asset_);
            marker_asset_ = nullptr;
        }
    }
}


void ControlsManager::update(const Input& input) {
    dx_ = dy_ = 0;

    if (input.isKeyDown(SDLK_SPACE) || input.isKeyDown(SDLK_q)) {
        handle_teleport(input);
    }
    movement(input);
    if (input.isKeyDown(SDLK_e)) {
        interaction();
    }
}

int ControlsManager::get_dx() const { return dx_; }
int ControlsManager::get_dy() const { return dy_; }
