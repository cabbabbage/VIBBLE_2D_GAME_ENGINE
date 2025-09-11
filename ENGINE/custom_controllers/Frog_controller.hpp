#ifndef FROG_CONTROLLER_HPP
#define FROG_CONTROLLER_HPP

#include "asset/asset_controller.hpp"

class Asset;
class Input;

class FrogController : public AssetController {

public:
    FrogController(Asset* self);
    ~FrogController() override = default;
    void update(const Input& in) override;

private:
    Asset* self_ = nullptr;
};

#endif
