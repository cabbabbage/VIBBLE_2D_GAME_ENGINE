#ifndef BOMB_CONTROLLER_HPP
#define BOMB_CONTROLLER_HPP

#include "asset/asset_controller.hpp"

class Assets;
class Asset;
class ActiveAssetsManager;
class Input;
class Area;

class BombController : public AssetController {
 public:
  BombController(Assets* assets, Asset* self, ActiveAssetsManager& aam);
  BombController(Assets* assets, Asset* self, ActiveAssetsManager& aam, Asset* player);
  ~BombController();

  void update(const Input& in) override;

 private:
  bool aabb(const Area& A, const Area& B) const;
  bool pointInAABB(int x, int y, const Area& B) const;
  bool canMove(int offset_x, int offset_y) const;

  void think_random();
  void pursue();
  bool try_hop_dirs(const char* const* names, const int* dx, const int* dy, int n);
  void explode_if_close();

  int randu();
  int rand_range(int lo, int hi);
  bool coin(int percent_true);

 private:
  Assets* assets_ = nullptr;
  Asset*  self_   = nullptr;
  ActiveAssetsManager& aam_;
  Asset* player_   = nullptr;

  int frames_until_think_ = 0;
  int think_interval_min_ = 30;
  int think_interval_max_ = 90;
  int probe_              = 24;

  int follow_radius_      = 500;
  int explode_radius_     = 20;

  unsigned int rng_seed_  = 0xB00B1Eu;
};

#endif
