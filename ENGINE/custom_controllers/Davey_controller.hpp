#ifndef DAVEY_CONTROLLER_HPP
#define DAVEY_CONTROLLER_HPP

#include "asset/asset_controller.hpp"

class Assets;
class Asset;
class Input;

class DaveyController : public AssetController {

public:
    DaveyController(Assets* assets, Asset* self);
    ~DaveyController() = default;
    void update(const Input& in) override;

private:
    enum class State { Idle, Pursuing, Orbiting };

    void enter_idle(int rest_ratio);
    void enter_pursue(Asset* target);
    void enter_orbit(Asset* center, int radius);
    void apply_path_bias(double desired_bias);

    Assets* assets_ = nullptr;
    Asset*  self_   = nullptr;
    State state_ = State::Idle;
    int idle_ratio_ = 5;
    Asset* current_target_ = nullptr;
    double default_bias_ = 0.7;
    double orbit_bias_ = 0.9;
    double active_bias_ = default_bias_;
};

#endif
