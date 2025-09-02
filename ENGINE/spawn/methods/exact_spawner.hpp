#pragma once

struct SpawnInfo;
class Area;
class SpawnContext;

// Spawns an asset at exact pixel coordinates provided in SpawnInfo
class ExactSpawner {
public:
    void spawn(const SpawnInfo& item, const Area* area, SpawnContext& ctx);
};

