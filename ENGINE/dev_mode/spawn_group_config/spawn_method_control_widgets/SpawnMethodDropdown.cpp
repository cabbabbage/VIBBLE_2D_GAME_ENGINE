#include "SpawnMethodDropdown.hpp"

#include <algorithm>
#include <utility>

namespace vibble::dev_mode::spawn_group_config::spawn_method_control_widgets {

SpawnMethodDropdown::SpawnMethodDropdown() = default;

void SpawnMethodDropdown::set_available_methods(std::vector<model::SpawnMethodId> methods) {
    available_methods_ = std::move(methods);
    if (!available_methods_.empty()) {
        if (selected_method_.empty()) {
            set_selected_method(available_methods_.front());
        } else if (std::find(available_methods_.begin(), available_methods_.end(), selected_method_) ==
                   available_methods_.end()) {
            set_selected_method(available_methods_.front());
        }
    } else if (!selected_method_.empty()) {
        set_selected_method(model::SpawnMethodId{});
    }
}

void SpawnMethodDropdown::set_selected_method(model::SpawnMethodId method) {
    if (selected_method_ == method) return;
    selected_method_ = std::move(method);
    on_method_selected_.emit(selected_method_);
}

const model::SpawnMethodId& SpawnMethodDropdown::selected_method() const {
    return selected_method_;
}

const std::vector<model::SpawnMethodId>& SpawnMethodDropdown::available_methods() const {
    return available_methods_;
}

Signal<const model::SpawnMethodId&>& SpawnMethodDropdown::on_method_selected() {
    return on_method_selected_;
}

}
