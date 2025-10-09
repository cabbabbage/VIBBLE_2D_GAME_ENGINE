#pragma once

#include "dev_mode/room_config/spawn_group_model.hpp"

namespace vibble::dev_mode::room_config::spawn_group_lists::spawn_method_control_widgets {

class ISpawnMethodWidget {
public:
    virtual ~ISpawnMethodWidget();

    virtual void bind_group(const model::SpawnGroup& group) = 0;
    virtual model::MethodConfig emit_config() const = 0;
};

}  // namespace vibble::dev_mode::room_config::spawn_group_lists::spawn_method_control_widgets
