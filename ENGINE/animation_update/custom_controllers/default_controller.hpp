#pragma once
#include "asset/asset_controller.hpp"

class Asset;
class Input;

class DefaultController : public AssetController {

public:
    DefaultController(Asset* self);
    ~DefaultController() override = default;
    void update(const Input& in) override;

private:
    Asset* self_ = nullptr;
};

