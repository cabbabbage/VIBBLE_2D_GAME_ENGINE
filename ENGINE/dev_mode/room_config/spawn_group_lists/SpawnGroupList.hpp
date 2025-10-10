#pragma once

#include <memory>
#include <vector>

#include "dev_mode/room_config/spawn_group_model.hpp"

namespace vibble::dev_mode::room_config::spawn_group_lists {

class SpawnGroupRow;

class SpawnGroupList {
public:
    SpawnGroupList();
    ~SpawnGroupList();

    void set_groups(std::vector<model::SpawnGroup> groups);
    const std::vector<std::unique_ptr<SpawnGroupRow>>& rows() const;

private:
    std::vector<std::unique_ptr<SpawnGroupRow>> rows_;
};

}  // namespace vibble::dev_mode::room_config::spawn_group_lists
