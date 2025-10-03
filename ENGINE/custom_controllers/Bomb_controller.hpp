#ifndef BOMB_CONTROLLER_HPP
#define BOMB_CONTROLLER_HPP

#include "asset/asset_controller.hpp"

class Assets;
class Asset;
class Input;

class BombController : public AssetController {

public:
    BombController(Assets* assets, Asset* self);
    ~BombController() override = default;
    void update(const Input& in) override;

private:
    enum class State { Idle, Pursuing, Detonating };

    void enter_idle(int rest_ratio);
    void enter_pursue(Asset* target);
    void trigger_explosion();

    Assets* assets_ = nullptr;
    Asset*  self_   = nullptr;
    State state_ = State::Idle;
    int idle_ratio_ = 5;
    Asset* current_target_ = nullptr;
};

#endif
