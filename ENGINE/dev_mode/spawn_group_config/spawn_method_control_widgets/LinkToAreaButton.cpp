#include "LinkToAreaButton.hpp"

#include <utility>

namespace vibble::dev_mode::spawn_group_config::spawn_method_control_widgets {

LinkToAreaButton::LinkToAreaButton() = default;

void LinkToAreaButton::set_target_area(std::string area_id) {
    area_id_ = std::move(area_id);
}

const std::string& LinkToAreaButton::target_area() const {
    return area_id_;
}

bool LinkToAreaButton::has_target_area() const {
    return !area_id_.empty();
}

Signal<const std::string&>& LinkToAreaButton::on_open_area() {
    return on_open_area_;
}

void LinkToAreaButton::open_area() {
    if (!area_id_.empty()) {
        on_open_area_.emit(area_id_);
    }
}

}
