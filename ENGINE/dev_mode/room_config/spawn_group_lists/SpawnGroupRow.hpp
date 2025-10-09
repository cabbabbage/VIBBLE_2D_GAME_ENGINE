#pragma once

#include <memory>

#include "dev_mode/room_config/spawn_group_model.hpp"

namespace vibble::dev_mode::room_config::spawn_group_lists {

namespace spawn_method_control_widgets {
class ISpawnMethodWidget;
}

class SpawnGroupRow {
public:
    explicit SpawnGroupRow(model::SpawnGroup group);
    ~SpawnGroupRow();

    const model::SpawnGroup& group() const;
    void set_group(model::SpawnGroup group);

    void set_method_widget(std::unique_ptr<spawn_method_control_widgets::ISpawnMethodWidget> widget);
    spawn_method_control_widgets::ISpawnMethodWidget* method_widget();
    const spawn_method_control_widgets::ISpawnMethodWidget* method_widget() const;

private:
    model::SpawnGroup group_;
    std::unique_ptr<spawn_method_control_widgets::ISpawnMethodWidget> method_widget_;
};

}  // namespace vibble::dev_mode::room_config::spawn_group_lists
