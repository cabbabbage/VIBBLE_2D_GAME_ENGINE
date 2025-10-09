#pragma once

#include <string>

#include "dev_mode/room_config/spawn_group_lists/Signal.hpp"

namespace vibble::dev_mode::room_config::spawn_group_lists::spawn_method_control_widgets {

class LinkToAreaButton {
public:
    LinkToAreaButton();

    void set_target_area(std::string area_id);
    const std::string& target_area() const;
    bool has_target_area() const;

    Signal<const std::string&>& on_open_area();
    void open_area();

private:
    std::string area_id_;
    Signal<const std::string&> on_open_area_{};
};

}  // namespace vibble::dev_mode::room_config::spawn_group_lists::spawn_method_control_widgets
