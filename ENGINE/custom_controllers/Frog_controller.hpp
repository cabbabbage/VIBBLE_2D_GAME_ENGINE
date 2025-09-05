#ifndef FROG_CONTROLLER_HPP
#define FROG_CONTROLLER_HPP

#include "asset/asset_controller.hpp"

class Assets;
class Asset;
class ActiveAssetsManager;
class Input;
class Area;

class FrogController : public AssetController {
 public:
  FrogController(Assets* assets, Asset* self, ActiveAssetsManager& aam);
  ~FrogController();

  void update(const Input& in) override;

 private:
  void think();
  bool try_hop_any_dir();
  bool canMove(int offset_x, int offset_y);
  bool has_anim(const char* name) const;

  bool aabb(const Area& A, const Area& B) const;
  bool pointInAABB(int x, int y, const Area& B) const;

  int  randu();
  int  rand_range(int lo, int hi);
  bool coin(int percent_true);

 private:
  Assets* assets_ = nullptr;
  Asset*  self_   = nullptr;
  ActiveAssetsManager& aam_;

  int frames_until_think_ = 0;
  int think_interval_min_ = 45;
  int think_interval_max_ = 150;

  int probe_ = 24;

  int  frame_slow_div_ = 4;   // advance animation 1 out of 4 ticks
  int  frame_phase_     = 0;

  unsigned int rng_seed_ = 0xC0FFEEu;
};

#endif
