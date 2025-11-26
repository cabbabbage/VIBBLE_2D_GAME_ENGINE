#ifndef BOMB_CONTROLLER_HPP
#define BOMB_CONTROLLER_HPP

#include "asset/asset_controller.hpp"

class Asset;
class Assets;
class Input;

class BombController : public AssetController {

public:
    BombController(Assets* assets, Asset* self);
    ~BombController() override = default;
    void update(const Input& in) override;

private:
    enum class State { Idle, Pursuing };

    void enter_pursue(Asset* target);
    static bool target_active(Asset* asset);
    Asset*      resolve_player_target() const;

    Assets* assets_ = nullptr;
    Asset*  self_   = nullptr;
    State   state_  = State::Idle;
    Asset*  current_target_ = nullptr;
};

#endif
