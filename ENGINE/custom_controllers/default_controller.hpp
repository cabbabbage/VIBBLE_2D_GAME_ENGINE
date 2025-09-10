#pragma once
#include "asset_controller.hpp"
#include "asset/animation_update.hpp"

class Asset;
class ActiveAssetsManager;

class DefaultController : public AssetController {

public:
    DefaultController(Asset* self, ActiveAssetsManager& aam);
    ~DefaultController() override = default;
    void update(const Input& in) override;

private:
    Asset*         self_ = nullptr;
    AnimationUpdate anim_;
};
