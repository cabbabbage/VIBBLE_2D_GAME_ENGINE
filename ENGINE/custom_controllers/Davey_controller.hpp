#ifndef Davey_CONTROLLER_HPP
#define Davey_CONTROLLER_HPP

#include "asset/asset_controller.hpp"     // base must be complete here

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
    Assets* assets_ = nullptr;  // non-owning
    Asset*  self_   = nullptr;  // controlled asset (non-player)
    ActiveAssetsManager& aam_;
};

#endif
