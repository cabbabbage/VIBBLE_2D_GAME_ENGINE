#pragma once
#include <string>

class Asset;

/*
  animation manager
  owns next animation queue and frame advance for an asset
*/
class AnimationManager {
public:
 explicit AnimationManager(Asset* owner);
 ~AnimationManager();
 void update();
private:
 Asset* self_ = nullptr;
 void apply_pending();
};
