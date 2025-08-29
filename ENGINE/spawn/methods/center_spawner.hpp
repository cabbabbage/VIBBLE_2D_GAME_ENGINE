#pragma once

class SpawnInfo;
class Area;
class SpawnContext;

class CenterSpawner {
public:
    void spawn(const SpawnInfo& item, const Area* area, SpawnContext& ctx);
};

