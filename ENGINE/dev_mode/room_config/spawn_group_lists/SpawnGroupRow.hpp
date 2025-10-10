#pragma once

#include <memory>
#include <string>
#include <vector>

#include "dev_mode/room_config/spawn_group_model.hpp"
#include "spawn_method_control_widgets/LinkToAreaButton.hpp"
#include "spawn_method_control_widgets/SpawnMethodDropdown.hpp"
#include "Signal.hpp"

namespace vibble::dev_mode::room_config::spawn_group_lists {

namespace spawn_method_control_widgets {
class LinkToAreaButton;
class SpawnMethodDropdown;
}

namespace widgets {
class ISpawnMethodWidget;
}

class SpawnGroupRow {
public:
    explicit SpawnGroupRow(model::SpawnGroup group);
    ~SpawnGroupRow();

    struct Header {
        std::string name;
        spawn_method_control_widgets::LinkToAreaButton link_to_area;
        spawn_method_control_widgets::SpawnMethodDropdown method_dropdown;
    };

    struct Body {
        widgets::ISpawnMethodWidget* method_widget = nullptr;
    };

    const model::SpawnGroup& group() const;
    void set_group(model::SpawnGroup group);

    Header& header();
    const Header& header() const;
    Body& body();
    const Body& body() const;

    void set_display_name(std::string name);
    const std::string& display_name() const;

    void set_area_id(std::string area_id);
    const std::string& area_id() const;

    void set_available_methods(std::vector<model::SpawnMethodId> methods);

    bool is_open() const;
    void set_open(bool open);

    Signal<>& on_open();
    Signal<>& on_move_up();
    Signal<>& on_move_down();
    Signal<>& on_delete();

    void trigger_open();
    void trigger_move_up();
    void trigger_move_down();
    void trigger_delete();

    void set_method_widget(std::unique_ptr<widgets::ISpawnMethodWidget> widget);
    widgets::ISpawnMethodWidget* method_widget();
    const widgets::ISpawnMethodWidget* method_widget() const;

private:
    void refresh_from_group();

    model::SpawnGroup group_;
    Header header_{};
    Body body_{};
    std::string area_id_{};
    bool open_ = false;
    bool suppress_method_change_ = false;
    Signal<> on_open_{};
    Signal<> on_move_up_{};
    Signal<> on_move_down_{};
    Signal<> on_delete_{};
    std::unique_ptr<widgets::ISpawnMethodWidget> method_widget_;
};

}  // namespace vibble::dev_mode::room_config::spawn_group_lists
