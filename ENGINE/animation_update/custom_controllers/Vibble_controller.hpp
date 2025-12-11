#ifndef VIBBLE_CONTROLLER_HPP
#define VIBBLE_CONTROLLER_HPP

#include "asset/asset_controller.hpp"
#include <SDL.h>
#include <string>
#include <chrono>
#include <thread>

class Asset;
class Input;

class VibbleController : public AssetController {

public:
    VibbleController(Asset* player);
    ~VibbleController() = default;
    void update(const Input& in) override;
    int get_dx() const;
    int get_dy() const;

private:
    void movement(const Input& input);
    float frame_dt() const;
    std::string animation_for_direction(int raw_x, int raw_y) const;
    void Dash();

    static constexpr float kWalkSpeed        = 300.0f;
    static constexpr float kSprintMultiplier = 2.0f;

    Asset* player_ = nullptr;
    int    dx_ = 0;
    int    dy_ = 0;

    bool canDash    = true;
    bool isDashing  = false;
    float dashingPower = 10.0f;
    float dashingTime = 0.05f;
    float dashingCooldown = 1.0f;
    std::chrono::steady_clock::time_point dashEndTime;
    std::chrono::steady_clock::time_point cooldownEndTime;

    float subpixel_x_ = 0.0f;
    float subpixel_y_ = 0.0f;
};

#endif
