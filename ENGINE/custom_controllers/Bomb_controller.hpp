#ifndef BOMB_CONTROLLER_HPP
#define BOMB_CONTROLLER_HPP

#include "asset/asset_controller.hpp"
#include "asset/auto_movement.hpp"

class Assets;
class Asset;
class ActiveAssetsManager;
class Input;

class BombController : public AssetController {
 public:
  BombController(Assets* assets, Asset* self, ActiveAssetsManager& aam);
  ~BombController();
  void update(const Input& in) override;
 private:
  void think_random();
  void pursue(Asset* player);
  void explosion_if_close(Asset* player);
  int randu();
  int rand_range(int lo, int hi);
  bool coin(int percent_true);
 private:
  Assets* assets_ = nullptr;
  Asset*  self_   = nullptr;
  ActiveAssetsManager& aam_;
  AutoMovement mover_;
  int probe_              = 24;
  int follow_radius_      = 1000;
  int explosion_radius_   = 150;
  bool updated_by_determine_ = false;
  unsigned int rng_seed_  = 0xB00B1Eu;
  // Persistent movement target; updated on animation end
  int  move_target_x_ = 0;
  int  move_target_y_ = 0;
  bool have_target_   = false;
};

#endif
