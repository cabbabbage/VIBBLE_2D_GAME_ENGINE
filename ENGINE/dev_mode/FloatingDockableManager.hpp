#pragma once

#include <functional>
#include <string>
#include <vector>

class DockableCollapsible;

class FloatingDockableManager {
public:
    using CloseCallback = std::function<void()>;

    static FloatingDockableManager& instance();

    void open_floating(const std::string& name,
                       DockableCollapsible* panel,
                       CloseCallback close_callback = {},
                       const std::string& stack_key = {});

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
