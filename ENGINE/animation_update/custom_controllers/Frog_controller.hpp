#ifndef FROG_CONTROLLER_HPP
#define FROG_CONTROLLER_HPP

#include "asset/asset_controller.hpp"

#include <SDL.h>

#include <random>
#include <unordered_map>
#include <unordered_set>

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
    void enter_run();

    void schedule_next_idle_hop();
    void perform_idle_hop();
    void schedule_next_run_hop();
    void perform_run_hop(Asset* threat);
    Asset* find_nearest_moving_threat(double radius);
    bool   is_asset_moving(Asset* candidate);
    void   prune_stale_positions(const std::unordered_set<Asset*>& seen);
    SDL_Point random_idle_destination();
    SDL_Point flee_destination(Asset* threat);

    Assets* assets_ = nullptr;
    Asset*  self_   = nullptr;
    State state_ = State::Idle;
    int idle_ratio_ = 55;
    Asset* last_run_target_ = nullptr;
    Uint32 next_idle_hop_time_ms_ = 0;
    Uint32 next_run_hop_time_ms_ = 0;
    bool threat_in_range_last_tick_ = false;
    std::unordered_map<Asset*, SDL_Point> last_known_positions_;
    std::mt19937 rng_;
};

#endif

