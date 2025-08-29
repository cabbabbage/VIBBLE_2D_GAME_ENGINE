#pragma once

class SpawnInfo;
class Area;
class SpawnContext;

class RandomSpawner {
public:
    void spawn(const SpawnInfo& item, const Area* area, SpawnContext& ctx);
};

