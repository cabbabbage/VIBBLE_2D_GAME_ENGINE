#include "SpawnMethodDropdown.hpp"

#include <algorithm>
#include <utility>

namespace vibble::dev_mode::room_config::spawn_group_lists::spawn_method_control_widgets {

SpawnMethodDropdown::SpawnMethodDropdown() = default;

void SpawnMethodDropdown::set_available_methods(std::vector<model::SpawnMethodId> methods) {
    available_methods_ = std::move(methods);
    if (!available_methods_.empty() && selected_method_.empty()) {
        selected_method_ = available_methods_.front();
    } else if (!selected_method_.empty()) {
        const auto it = std::find(available_methods_.begin(), available_methods_.end(), selected_method_);
        if (it == available_methods_.end()) {
            selected_method_.clear();
        }
    }
}

void SpawnMethodDropdown::set_selected_method(model::SpawnMethodId method) {
    selected_method_ = std::move(method);
}

const model::SpawnMethodId& SpawnMethodDropdown::selected_method() const {
    return selected_method_;
}

const std::vector<model::SpawnMethodId>& SpawnMethodDropdown::available_methods() const {
    return available_methods_;
}

}  // namespace vibble::dev_mode::room_config::spawn_group_lists::spawn_method_control_widgets
