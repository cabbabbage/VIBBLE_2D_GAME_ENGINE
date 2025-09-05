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
    DaveyController(Assets* assets, Asset* player, ActiveAssetsManager& aam);
    ~DaveyController() = default;

    void update(const Input& in) override;

    int get_dx() const;
    int get_dy() const;

   private:
    bool aabb(const Area& A, const Area& B) const;
    bool pointInAABB(int x, int y, const Area& B) const;

    void movement(const Input& input);
    bool canMove(int offset_x, int offset_y);
    void interaction();
    void handle_teleport(const Input& input);

   private:
    Assets* assets_ = nullptr;
    Asset*  player_ = nullptr;
    ActiveAssetsManager& aam_;
    int dx_ = 0;
    int dy_ = 0;

    struct Point { int x=0; int y=0; };
    Point teleport_point_{};
    bool teleport_set_ = false;
    Asset* marker_asset_ = nullptr;
};

#endif
