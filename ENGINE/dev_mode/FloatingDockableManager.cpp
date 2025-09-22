#include "FloatingDockableManager.hpp"

#include <utility>

#include "DockableCollapsible.hpp"

FloatingDockableManager& FloatingDockableManager::instance() {
    static FloatingDockableManager manager;
    return manager;
}

void FloatingDockableManager::open_floating(const std::string& name,
                                            DockableCollapsible* panel,
                                            CloseCallback close_callback) {
    if (!panel) {
        return;
    }

    if (current_.panel == panel) {
        current_.name = name;
        current_.close_callback = std::move(close_callback);
        return;
    }

    if (current_.panel && current_.panel != panel) {
        ActiveEntry previous = std::move(current_);
        current_ = ActiveEntry{};
        if (previous.close_callback) {
            previous.close_callback();
        } else if (previous.panel) {
            previous.panel->set_visible(false);
        }
    }

    current_.panel = panel;
    current_.name = name;
    current_.close_callback = std::move(close_callback);
}

void FloatingDockableManager::notify_panel_closed(const DockableCollapsible* panel) {
    if (panel && current_.panel == panel) {
        current_ = ActiveEntry{};
    }
}
