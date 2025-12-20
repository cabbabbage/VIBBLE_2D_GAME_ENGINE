#pragma once
#include "asset/asset_controller.hpp"

#include <random>

class Assets;
class Asset;
class Input;

class GaryController : public AssetController {
public:
    GaryController(Assets* assets, Asset* self);
    ~GaryController() override = default;

    void init();

    void update(const Input& in) override;

private:
    Assets* assets_ = nullptr;
    Asset*  self_   = nullptr;

    std::mt19937 rng_;
    std::uniform_int_distribution<int> idle_range_{};
};
