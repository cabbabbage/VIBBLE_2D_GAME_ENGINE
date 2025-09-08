#pragma once

struct SpawnInfo;
class Area;
class SpawnContext;

class DistributedSpawner {

	public:
    void spawn(const SpawnInfo& item, const Area* area, SpawnContext& ctx);
};
