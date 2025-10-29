#ifndef VIBBLE_CONTROLLER_HPP
#define VIBBLE_CONTROLLER_HPP

#include "asset/asset_controller.hpp"
#include <SDL.h>
#include <string>

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
    std::string animation_for_direction(int raw_x, int raw_y) const;

    static constexpr int kWalkSpeed       = 5;
    static constexpr int kSprintMultiplier = 2;

    Asset* player_ = nullptr;
    int    dx_ = 0;
    int    dy_ = 0;
};

#endif

