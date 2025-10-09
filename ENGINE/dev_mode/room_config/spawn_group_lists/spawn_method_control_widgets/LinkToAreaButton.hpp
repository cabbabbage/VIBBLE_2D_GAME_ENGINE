#pragma once

#include <string>

namespace vibble::dev_mode::room_config::spawn_group_lists::spawn_method_control_widgets {

class LinkToAreaButton {
public:
    LinkToAreaButton();

    void set_target_area(std::string area_id);
    const std::string& target_area() const;
    bool has_target_area() const;

private:
    std::string area_id_;
};

}  // namespace vibble::dev_mode::room_config::spawn_group_lists::spawn_method_control_widgets
