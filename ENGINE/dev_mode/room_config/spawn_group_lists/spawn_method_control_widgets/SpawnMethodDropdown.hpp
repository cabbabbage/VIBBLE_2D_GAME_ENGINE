#pragma once

#include <vector>

#include "dev_mode/room_config/spawn_group_model.hpp"
#include "dev_mode/room_config/spawn_group_lists/Signal.hpp"

namespace vibble::dev_mode::room_config::spawn_group_lists::spawn_method_control_widgets {

class SpawnMethodDropdown {
public:
    SpawnMethodDropdown();

    void set_available_methods(std::vector<model::SpawnMethodId> methods);
    void set_selected_method(model::SpawnMethodId method);
    const model::SpawnMethodId& selected_method() const;
    const std::vector<model::SpawnMethodId>& available_methods() const;

    Signal<void(const model::SpawnMethodId&)>& on_method_selected();

private:
    std::vector<model::SpawnMethodId> available_methods_;
    model::SpawnMethodId selected_method_;
    Signal<void(const model::SpawnMethodId&)> on_method_selected_;
};

}  // namespace vibble::dev_mode::room_config::spawn_group_lists::spawn_method_control_widgets
