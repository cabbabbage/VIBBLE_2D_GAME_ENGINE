#include "FloatingDockableManager.hpp"

#include <algorithm>
#include <utility>

#include "DockableCollapsible.hpp"

FloatingDockableManager& FloatingDockableManager::instance() {
    static FloatingDockableManager manager;
    return manager;
}

namespace {

void close_entry(FloatingDockableManager::CloseCallback& callback,
                 DockableCollapsible* panel) {
    if (callback) {
        callback();
    } else if (panel) {
        panel->set_visible(false);
    }
}

}

void FloatingDockableManager::open_floating(const std::string& name,
                                            DockableCollapsible* panel,
                                            CloseCallback close_callback,
                                            const std::string& stack_key) {
    if (!panel) {
        return;
    }

    if (current_.panel == panel) {
        current_.name = name;
        current_.close_callback = std::move(close_callback);
        current_.stack_key = stack_key;
        return;
    }

    const bool share_stack = !stack_key.empty() && stack_key == current_.stack_key;

    if (!share_stack) {
        if (current_.panel) {
            ActiveEntry previous = std::move(current_);
            current_ = ActiveEntry{};
            if (previous.panel != panel) {
                close_entry(previous.close_callback, previous.panel);
            }
        }
        while (!stack_.empty()) {
            ActiveEntry entry = std::move(stack_.back());
            stack_.pop_back();
            if (entry.panel == panel) {
                continue;
            }
            close_entry(entry.close_callback, entry.panel);
        }
    } else if (current_.panel) {

        stack_.erase(std::remove_if(stack_.begin(), stack_.end(),
                                    [panel](const ActiveEntry& entry) {
                                        return entry.panel == panel;
                                    }),
                     stack_.end());
        stack_.push_back(std::move(current_));
        current_ = ActiveEntry{};
    }

    current_.panel = panel;
    current_.name = name;
    current_.close_callback = std::move(close_callback);
    current_.stack_key = stack_key;
}

void FloatingDockableManager::notify_panel_closed(const DockableCollapsible* panel) {
    if (!panel) {
        return;
    }

    if (current_.panel == panel) {
        current_ = ActiveEntry{};
        if (!stack_.empty()) {
            current_ = std::move(stack_.back());
            stack_.pop_back();
        }
        return;
    }

    stack_.erase(std::remove_if(stack_.begin(), stack_.end(),
                                [panel](const ActiveEntry& entry) {
                                    return entry.panel == panel;
                                }),
                 stack_.end());
}
