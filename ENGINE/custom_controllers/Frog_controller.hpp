#ifndef FROG_CONTROLLER_HPP
#define FROG_CONTROLLER_HPP

#include "asset/asset_controller.hpp"

class Assets;
class Asset;
class Input;

class FrogController : public AssetController {

public:

    FrogController(Assets* assets, Asset* self);

    ~FrogController() override = default;
    void update(const Input& in) override;

private:

    Assets* assets_ = nullptr;
    Asset*  self_   = nullptr;

};

#endif
