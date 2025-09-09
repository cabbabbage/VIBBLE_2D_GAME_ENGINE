#pragma once
#include "asset_controller.hpp"
#include "asset/animation_update.hpp"

class Assets;
class Asset;
class ActiveAssetsManager;

class DefaultController : public AssetController {

	public:
    DefaultController(Assets* assets, Asset* self, ActiveAssetsManager& aam);
    ~DefaultController() override;
    void update(const Input& in) override;

	private:
    Assets* assets_ = nullptr;
    Asset*  self_   = nullptr;
    ActiveAssetsManager& aam_;
    AnimationUpdate anim_;
};
