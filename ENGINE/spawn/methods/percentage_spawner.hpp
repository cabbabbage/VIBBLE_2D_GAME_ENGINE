#pragma once

#include <utility>

struct SpawnInfo;
class Area;
class SpawnContext;

class PercentageSpawner {
public:
    void spawn(const SpawnInfo& item, const Area* area, SpawnContext& ctx);
};

