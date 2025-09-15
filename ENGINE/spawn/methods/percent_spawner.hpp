#pragma once

struct SpawnInfo;
class Area;
class SpawnContext;

class PercentSpawner {
public:
    void spawn(const SpawnInfo& item, const Area* area, SpawnContext& ctx);
};

