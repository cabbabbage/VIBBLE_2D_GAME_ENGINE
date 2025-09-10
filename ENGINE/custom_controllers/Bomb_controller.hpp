#ifndef BOMB_CONTROLLER_HPP
#define BOMB_CONTROLLER_HPP

#include "asset/asset_controller.hpp"
#include "asset/animation_update.hpp"

class Assets;
class Asset;
class ActiveAssetsManager;
class Input;

class BombController : public AssetController {

public:
    BombController(Assets* assets, Asset* self, ActiveAssetsManager& aam);
    ~BombController() override = default;
    void update(const Input& in) override;

private:
    void think_random();
    void pursue(Asset* player);
    // Now returns true if explosion triggered (to stop further logic this frame)
    bool explosion_if_close(Asset* player);

    Assets*        assets_ = nullptr;
    Asset*         self_   = nullptr;
    AnimationUpdate anim_;
    int            probe_               = 24;
    int            follow_radius_       = 1000;
    int            explosion_radius_    = 150;
    int            follow_radius_sq_    = follow_radius_ * follow_radius_;
    int            explosion_radius_sq_ = explosion_radius_ * explosion_radius_;
};

#endif
