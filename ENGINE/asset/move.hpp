#pragma once

#include "asset/animation.hpp"

class Asset;

class Move {

	public:
    static void apply(Asset* self, const Animation::FrameMovement& fm);
};
