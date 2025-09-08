#pragma once

#include <utility>

struct SpawnInfo;
class Area;
class SpawnContext;

class ExactSpawner {

	public:
    void spawn(const SpawnInfo& item, const Area* area, SpawnContext& ctx);
};
