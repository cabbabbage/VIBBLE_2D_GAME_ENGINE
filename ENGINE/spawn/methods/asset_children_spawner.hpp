#pragma once

#include "spawn_info.hpp"
class Area;
class SpawnContext;

class ChildrenSpawner {
public:
    void spawn(const SpawnInfo& item, const Area* area, SpawnContext& ctx);
};

