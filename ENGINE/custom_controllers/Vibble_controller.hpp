#ifndef VIBBLE_CONTROLLER_HPP
#define VIBBLE_CONTROLLER_HPP

#include "asset/asset_controller.hpp"
#include <SDL.h>

class Asset;
class Input;

class VibbleController : public AssetController {

public:
    VibbleController(Asset* player);
    ~VibbleController() = default;
    void update(const Input& in) override;
    int get_dx() const;
    int get_dy() const;

private:
    void movement(const Input& input);

    Asset* player_ = nullptr;
    int    dx_ = 0;
    int    dy_ = 0;
};

#endif
