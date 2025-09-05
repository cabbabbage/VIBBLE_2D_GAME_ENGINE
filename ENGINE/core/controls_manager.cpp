
#include "controls_manager.hpp"
#include "active_assets_manager.hpp"
#include "core/AssetsManager.hpp"
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
    if (!player_) {
        std::cerr << "[ControlsManager::handle_teleport][Error] player_ is null, cannot teleport\n";
        return;
    }

    std::cout << "\n[ControlsManager::handle_teleport] Handling teleport input\n";
    std::cout << "[Debug] Player position: (" << player_->pos_X << ", " << player_->pos_Y << ")\n";

    // --- Set teleport point and drop a marker ---
    if (input.wasKeyPressed(SDLK_SPACE) && !teleport_set_) {
        std::cout << "[Teleport] SPACE pressed -> setting teleport point\n";

        teleport_point_ = { player_->pos_X, player_->pos_Y };
        teleport_set_ = true;
        std::cout << "[Teleport] Teleport point set at (" 
                  << teleport_point_.x << ", " << teleport_point_.y << ")\n";

        // Remove existing marker
        if (marker_asset_ && assets_) {
            std::cout << "[Teleport] Removing existing marker at " << marker_asset_ << "\n";
            aam_.remove(marker_asset_);
            assets_->remove(marker_asset_);
            aam_.updateClosestAssets(player_, 3);
            marker_asset_ = nullptr;
            std::cout << "[Teleport] Marker removed successfully\n";
        }

        // Spawn new marker nearby
        if (assets_) {
            std::cout << "[Teleport] Spawning new marker asset near player\n";
            static std::mt19937 rng{ std::random_device{}() };
            std::uniform_real_distribution<float> angle(0.0f, 6.2831853f);

            float a = angle(rng);
            int r   = 30; // radius
            int mx  = player_->pos_X + static_cast<int>(std::round(std::cos(a) * r));
            int my  = player_->pos_Y + static_cast<int>(std::round(std::sin(a) * r));

            std::cout << "[Teleport][Debug] Marker spawn coords: (" << mx << ", " << my 
                      << ") offset " << r << "px @ angle " << a << " radians\n";

            marker_asset_ = assets_->spawn_asset("marker", mx, my);
            if (marker_asset_) {
                std::cout << "[Teleport] Marker asset spawned successfully at " << marker_asset_ << "\n";
            } else {
                std::cerr << "[Teleport][Error] Failed to spawn marker asset\n";
            }
        } else {
            std::cerr << "[Teleport][Error] assets_ is null, cannot spawn marker\n";
        }
    }

    // --- Teleport on 'Q' press ---
    if (input.wasKeyPressed(SDLK_q) && teleport_set_) {
        std::cout << "[Teleport] Q pressed -> teleporting player to ("
                  << teleport_point_.x << ", " << teleport_point_.y << ")\n";

        player_->set_position(teleport_point_.x, teleport_point_.y);

        // Reset teleport state
        teleport_point_ = { 0, 0 };
        teleport_set_   = false;
        std::cout << "[Teleport] Teleport completed, reset point and state\n";

        // Remove marker if it exists
        if (marker_asset_ && assets_) {
            std::cout << "[Teleport] Removing marker after teleport at " << marker_asset_ << "\n";
            aam_.remove(marker_asset_);
            assets_->remove(marker_asset_);
            aam_.updateClosestAssets(player_, 3);
            marker_asset_ = nullptr;
            std::cout << "[Teleport] Marker cleaned up after teleport\n";
        } else {
            std::cout << "[Teleport] No marker to remove after teleport\n";
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
