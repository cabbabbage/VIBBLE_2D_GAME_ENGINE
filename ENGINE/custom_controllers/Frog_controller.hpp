#ifndef FROG_CONTROLLER_HPP
#define FROG_CONTROLLER_HPP

#include "asset/asset_controller.hpp"
#include "asset/animation_update.hpp"

class Asset;
class ActiveAssetsManager;
class Input;

class FrogController : public AssetController {

public:
    FrogController(Asset* self, ActiveAssetsManager& aam);
    ~FrogController() override = default;
    void update(const Input& in) override;

private:
    Asset*         self_ = nullptr;
    AnimationUpdate anim_;
};

#endif
