#include "SpawnGroupRow.hpp"

#include <utility>

#include "spawn_method_control_widgets/ISpawnMethodWidget.hpp"

namespace vibble::dev_mode::room_config::spawn_group_lists {

using spawn_method_control_widgets::ISpawnMethodWidget;

SpawnGroupRow::SpawnGroupRow(model::SpawnGroup group) : group_(std::move(group)) {}

SpawnGroupRow::~SpawnGroupRow() = default;

const model::SpawnGroup& SpawnGroupRow::group() const {
    return group_;
}

void SpawnGroupRow::set_group(model::SpawnGroup group) {
    group_ = std::move(group);
}

void SpawnGroupRow::set_method_widget(std::unique_ptr<ISpawnMethodWidget> widget) {
    method_widget_ = std::move(widget);
}

ISpawnMethodWidget* SpawnGroupRow::method_widget() {
    return method_widget_.get();
}

const ISpawnMethodWidget* SpawnGroupRow::method_widget() const {
    return method_widget_.get();
}

}  // namespace vibble::dev_mode::room_config::spawn_group_lists
