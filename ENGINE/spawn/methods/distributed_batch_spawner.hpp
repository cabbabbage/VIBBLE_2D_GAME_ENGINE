#pragma once

#include <vector>

struct BatchSpawnInfo;
class Area;
class SpawnContext;

class DistributedBatchSpawner {

	public:
    void spawn(const std::vector<BatchSpawnInfo>& items, const Area* area, int spacing, int jitter, SpawnContext& ctx);
};
