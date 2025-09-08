#ifndef Davey_CONTROLLER_HPP
#define Davey_CONTROLLER_HPP

#include "asset/asset_controller.hpp"
#include "asset/auto_movement.hpp"

class Assets;
class Asset;
class ActiveAssetsManager;
class Input;
class Area;

class DaveyController : public AssetController {
   public:
    // 'self' is the asset controlled by this controller (non-player)
    DaveyController(Assets* assets, Asset* self, ActiveAssetsManager& aam);
    ~DaveyController() = default;
    void update(const Input& in) override;
   private:
    Assets* assets_ = nullptr;
    Asset*  self_   = nullptr;
    ActiveAssetsManager& aam_;
    AutoMovement mover_;
    // Pursuit target management (recompute every N frames)
    int pursue_target_x_ = 0;
    int pursue_target_y_ = 0;
    int pursue_frames_left_ = 0;
    int pursue_recalc_interval_ = 100;
};

#endif
