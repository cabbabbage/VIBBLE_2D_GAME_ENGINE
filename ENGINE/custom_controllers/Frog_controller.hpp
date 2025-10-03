#ifndef FROG_CONTROLLER_HPP
#define FROG_CONTROLLER_HPP

#include "asset/asset_controller.hpp"

class Assets;
class Asset;
class Input;

class FrogController : public AssetController {

public:

    FrogController(Assets* assets, Asset* self);

    ~FrogController() override = default;
    void update(const Input& in) override;

private:
    enum class State { Idle, Running };

    void enter_idle(int rest_ratio);
    void enter_run(Asset* threat);

    Assets* assets_ = nullptr;
    Asset*  self_   = nullptr;
    State state_ = State::Idle;
    int idle_ratio_ = 55;
    Asset* last_run_target_ = nullptr;
};

#endif
