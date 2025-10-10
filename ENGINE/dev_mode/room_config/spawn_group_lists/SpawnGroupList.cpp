#include "SpawnGroupList.hpp"

#include <memory>
#include <utility>

#include "SpawnGroupRow.hpp"

namespace vibble::dev_mode::room_config::spawn_group_lists {

SpawnGroupList::SpawnGroupList() = default;
SpawnGroupList::~SpawnGroupList() = default;

void SpawnGroupList::set_groups(std::vector<model::SpawnGroup> groups) {
    rows_.clear();
    rows_.reserve(groups.size());
    for (auto& group : groups) {
        rows_.push_back(std::make_unique<SpawnGroupRow>(std::move(group)));
    }
}

const std::vector<std::unique_ptr<SpawnGroupRow>>& SpawnGroupList::rows() const {
    return rows_;
}

}  // namespace vibble::dev_mode::room_config::spawn_group_lists
