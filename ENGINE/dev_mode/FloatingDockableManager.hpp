#pragma once

#include <functional>
#include <string>
#include <vector>

class DockableCollapsible;

// Global manager that ensures only a single floating DockableCollapsible panel
// is open at a time. Panels register with the manager when opening and the
// manager will close any previously-open floating panel.
class FloatingDockableManager {
public:
    using CloseCallback = std::function<void()>;

    static FloatingDockableManager& instance();

    // Open the provided floating panel. If another panel is currently active it
    // will be closed via its registered close callback before the new panel is
    // activated. The caller is responsible for making the panel visible after
    // this call succeeds.
    void open_floating(const std::string& name,
                       DockableCollapsible* panel,
                       CloseCallback close_callback = {},
                       const std::string& stack_key = {});

    // Notify the manager that a panel has been closed (either by the user or as
    // part of a teardown). The manager clears its active reference so future
    // openings are not blocked.
    void notify_panel_closed(const DockableCollapsible* panel);

    DockableCollapsible* active_panel() const { return current_.panel; }
    const std::string& active_name() const { return current_.name; }

private:
    FloatingDockableManager() = default;

    struct ActiveEntry {
        std::string name;
        DockableCollapsible* panel = nullptr;
        CloseCallback close_callback;
        std::string stack_key;
    };

    ActiveEntry current_{};
    std::vector<ActiveEntry> stack_;
};
