#ifndef LAB_BOMB_CONTROLLER_HPP
#define LAB_BOMB_CONTROLLER_HPP

#include "asset/asset_controller.hpp"

class Assets;
class Asset;
class Input;

class LabBombController : public AssetController {
public:
    LabBombController(Assets* assets, Asset* self);
    ~LabBombController() override = default;

    void update(const Input& in) override;

    void start_pursuit();
    void mark_spent();
    bool is_spent() const { return spent_; }

private:
    enum class State { Idle, Pursuing, Detonated };

    void enter_idle(int rest_ratio);
    void enter_pursue(Asset* target);
    bool trigger_explosion();

    Assets* assets_ = nullptr;
    Asset* self_ = nullptr;
    Asset* current_target_ = nullptr;
    State state_ = State::Idle;
    int idle_ratio_ = 5;
    bool spent_ = false;
    bool triggered_ = false;
};

#endif
