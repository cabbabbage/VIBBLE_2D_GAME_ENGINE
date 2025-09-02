#pragma once

#include <string>

// Simple unique ID generator for spawned assets.
// IDs are unique within a process and highly unlikely to collide.
// Format: asid-<counter>-<rand>
class AssetSpawnId {
public:
    static std::string generate();
};

