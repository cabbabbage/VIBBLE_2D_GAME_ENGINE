#pragma once

#include <string>
#include <vector>
#include <memory>
#include <SDL.h>

#include "utils/simple_tasks_markdown.hpp"

class DMButton;
class DMTextBox;
class DMDropdown;

namespace devmode::core {
class ManifestStore;
}

// Ctrl+T panel for simplified Markdown-backed tasks
class QuickTaskPopup {
public:
    QuickTaskPopup();
    ~QuickTaskPopup();

    void set_manifest_store(devmode::core::ManifestStore* store);

    // State management
    void open();
    bool is_open() const { return is_open_; }
    void close();

    // UI handlers
    void update();
    void render(SDL_Renderer* renderer);
    bool handle_event(const SDL_Event& event);

    // Escape key handling
    void handle_escape();

private:
    // Build and layout the simplified panel UI
    void rebuild_ui();
    void layout_ui(const SDL_Rect& screen_rect) const;

    // Task/actions
    void add_new_task();
    void delete_dev_task(size_t index);
    void delete_cline_task(size_t index);
    void persist_all();

    // UI state
    bool is_open_ = false;
    bool layout_dirty_ = true;

    // Data
    SimpleTasksFile dev_file_{"DEV_TASKS.md", "Dev Tasks"};
    SimpleTasksFile cline_file_{"CLINE_WORKFLOW.md", "Cline Workflow"};
    std::vector<SimpleTask> dev_tasks_;
    std::vector<SimpleTask> cline_tasks_;

    // Top bar widgets
    mutable std::unique_ptr<DMDropdown> assignee_dd_;
    mutable std::unique_ptr<DMDropdown> assigner_dd_;
    mutable std::unique_ptr<DMTextBox> description_box_;
    mutable std::unique_ptr<DMButton>  add_button_;

    // Section headers
    mutable std::unique_ptr<DMButton> dev_label_;
    mutable std::unique_ptr<DMButton> cline_label_;

    // Per-task row widgets
    struct RowWidgets {
        std::unique_ptr<DMButton> delete_button;
    };
    mutable std::vector<RowWidgets> dev_row_widgets_;
    mutable std::vector<RowWidgets> cline_row_widgets_;

    // Positioning and layout
    mutable SDL_Rect popup_rect_{};
    mutable SDL_Rect topbar_rect_{};
    mutable SDL_Rect lists_rect_{};
    mutable SDL_Rect dev_rect_{};
    mutable SDL_Rect cline_rect_{};

    devmode::core::ManifestStore* manifest_store_ = nullptr;
};
