#pragma once

#include <vector>

#include "dev_mode/room_config/spawn_group_model.hpp"

namespace vibble::dev_mode::room_config::spawn_group_lists::spawn_method_control_widgets {

class SpawnMethodDropdown {
public:
    SpawnMethodDropdown();

    void set_available_methods(std::vector<model::SpawnMethodId> methods);
    void set_selected_method(model::SpawnMethodId method);
    const model::SpawnMethodId& selected_method() const;
    const std::vector<model::SpawnMethodId>& available_methods() const;

private:
    std::vector<model::SpawnMethodId> available_methods_;
    model::SpawnMethodId selected_method_;
};

}  // namespace vibble::dev_mode::room_config::spawn_group_lists::spawn_method_control_widgets
