#pragma once

#include <memory>
#include <string>
#include <vector>
#include <functional>

#include <nlohmann/json.hpp>

#include "DockableCollapsible.hpp"

class DMButton;
class ButtonWidget;
class Widget;

// Reusable UI element that renders a list of spawn groups with action buttons.
// - Edit/Delete buttons use icon glyphs.
// - Up/Down buttons move a group in the list (priority editor).
//
// Note: This class only renders rows and wires callbacks. The owning panel is
// responsible for mutating the underlying JSON (e.g., reordering and
// renumbering priorities) and then reloading this list.
class SpawnGroupList {
public:
    using Rows = DockableCollapsible::Rows;

    struct Callbacks {
        std::function<void(const std::string&)> on_edit;      // open editor
        std::function<void(const std::string&)> on_duplicate; // duplicate
        std::function<void(const std::string&)> on_delete;    // delete
        std::function<void(const std::string&)> on_move_up;   // move up
        std::function<void(const std::string&)> on_move_down; // move down
    };

    SpawnGroupList();
    ~SpawnGroupList();

    // Provide groups array to render; does not take ownership.
    void load(const nlohmann::json& groups);

    // Set action callbacks.
    void set_callbacks(Callbacks cb);

    // Append UI rows for current groups list.
    void append_rows(Rows& rows);

private:
    struct RowWidgets {
        std::string id;
        std::unique_ptr<Widget> label;            // summary label
        std::unique_ptr<DMButton> btn_edit;
        std::unique_ptr<ButtonWidget> w_edit;
        std::unique_ptr<DMButton> btn_up;
        std::unique_ptr<ButtonWidget> w_up;
        std::unique_ptr<DMButton> btn_down;
        std::unique_ptr<ButtonWidget> w_down;
        std::unique_ptr<DMButton> btn_dup;
        std::unique_ptr<ButtonWidget> w_dup;
        std::unique_ptr<DMButton> btn_del;
        std::unique_ptr<ButtonWidget> w_del;
    };

    std::vector<std::unique_ptr<RowWidgets>> rows_;
    nlohmann::json snapshot_;
    Callbacks cbs_{};
};

