#pragma once

#include "asset/animation.hpp" // for Animation::FrameMovement

class Asset;

// Helper used by controllers to move an asset by a FrameMovement delta
// and handle z-index resorting when requested.
class Move {
public:
  static void apply(Asset* self, const Animation::FrameMovement& fm);
};

