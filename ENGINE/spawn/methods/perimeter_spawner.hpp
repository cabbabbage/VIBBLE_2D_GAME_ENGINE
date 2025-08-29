#pragma once

struct SpawnInfo;
class Area;
class SpawnContext;

class PerimeterSpawner {
public:
    void spawn(const SpawnInfo& item, const Area* area, SpawnContext& ctx);
};

