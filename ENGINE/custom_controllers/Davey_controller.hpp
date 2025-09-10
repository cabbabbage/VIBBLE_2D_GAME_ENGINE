#ifndef DAVEY_CONTROLLER_HPP
#define DAVEY_CONTROLLER_HPP

#include "asset/asset_controller.hpp"
#include "asset/animation_update.hpp"

class Assets;
class Asset;
class ActiveAssetsManager;
class Input;

class DaveyController : public AssetController {

public:
    DaveyController(Assets* assets, Asset* self, ActiveAssetsManager& aam);
    ~DaveyController() = default;
    void update(const Input& in) override;

private:
    Assets*        assets_ = nullptr;
    Asset*         self_   = nullptr;
    AnimationUpdate anim_;
};

#endif
