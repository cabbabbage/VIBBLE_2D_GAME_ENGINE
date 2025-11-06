#pragma once

#include <string>
#include <vector>
#include <memory>
#include <SDL.h>

#include "utils/quick_task_file_indexer.hpp"
#include "utils/dev_tasks_markdown.hpp"

class DMButton;
class DMTextBox;
class DMCheckbox;

namespace devmode::core {
class ManifestStore;
}

// Ctrl+T panel for Markdown-backed dev tasks with three lanes
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
    void delete_task(size_t index);
    void set_task_status(size_t index, DevTaskStatus status);
    void toggle_task_completed(size_t index, bool completed);
    void persist();

    // Helpers
    void group_tasks_by_lane();

    // UI state
    bool is_open_ = false;
    bool layout_dirty_ = true;
    bool show_completed_lane_ = true;

    // Data
    std::unique_ptr<QuickTaskFileIndexer> file_indexer_;
    DevTasksMarkdown tasks_io_;
    std::vector<DevTask> tasks_;

    // Top bar widgets
    mutable std::unique_ptr<DMTextBox> assignee_box_;
    mutable std::unique_ptr<DMTextBox> new_task_box_;
    mutable std::unique_ptr<DMButton>  add_button_;
    mutable std::unique_ptr<DMCheckbox> show_completed_checkbox_;

    // Lane header buttons (visual labels)
    mutable std::unique_ptr<DMButton> lane1_label_;
    mutable std::unique_ptr<DMButton> lane2_label_;
    mutable std::unique_ptr<DMButton> lane3_label_;

    // Per-task widgets
    struct TaskWidgets {
        std::unique_ptr<DMCheckbox> checkbox;
        std::unique_ptr<DMTextBox>  title_edit;
        std::unique_ptr<DMButton>   status_button;
        std::unique_ptr<DMButton>   delete_button;
    };
    mutable std::vector<TaskWidgets> task_widgets_;

    // Lane index lists
    mutable std::vector<size_t> lane1_indices_;
    mutable std::vector<size_t> lane2_indices_;
    mutable std::vector<size_t> lane3_indices_;

    // Positioning and layout
    mutable SDL_Rect popup_rect_{};
    mutable SDL_Rect topbar_rect_{};
    mutable SDL_Rect lanes_rect_{};
    mutable SDL_Rect lane1_rect_{};
    mutable SDL_Rect lane2_rect_{};
    mutable SDL_Rect lane3_rect_{};

    devmode::core::ManifestStore* manifest_store_ = nullptr;
};
