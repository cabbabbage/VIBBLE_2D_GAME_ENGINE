#ifndef LAB_BOMB_CONTROLLER_HPP
#define LAB_BOMB_CONTROLLER_HPP

#include "asset/asset_controller.hpp"

#include <string>

class Assets;
class Asset;
class Input;

class LabBombController : public AssetController {
public:
    LabBombController(Assets* assets, Asset* self);
    ~LabBombController() override;

    void update(const Input& in) override;

    void start_pursuit();
    void mark_spent();
    bool is_spent() const { return spent_; }

private:
    enum class State { Idle, Pursuing, Detonated };

    void enter_idle(int rest_ratio);
    void enter_pursue(Asset* target);
    bool trigger_explosion();
    bool ensure_registration();
    bool is_player_inside_trigger() const;
    bool is_in_owning_room() const;
    void process_pending_deletion();
    bool should_wait_for_explosion_animation() const;

    Assets* assets_ = nullptr;
    Asset* self_ = nullptr;
    Asset* current_target_ = nullptr;
    State state_ = State::Idle;
    int idle_ratio_ = 5;
    bool registered_ = false;
    bool spent_ = false;
    bool pending_deletion_ = false;
    std::string detonation_animation_;
};

#endif
