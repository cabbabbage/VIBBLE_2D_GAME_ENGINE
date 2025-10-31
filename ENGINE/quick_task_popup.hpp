#pragma once

#include <string>
#include <vector>
#include <unordered_set>
#include <memory>
#include <SDL.h>

#include "quick_task_file_indexer.hpp"
#include "quick_task_csv.hpp"

class DMButton;
class DMTextBox;

namespace devmode::core {
class ManifestStore;
}

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
    void render(SDL_Renderer* renderer) const;
    bool handle_event(const SDL_Event& event);

    // Escape key handling
    void handle_escape();

private:
    void rebuild_ui();
    void layout_ui(const SDL_Rect& screen_rect) const;
    void update_filtered_files();
    void select_file(const std::string& file_path);
    void deselect_file(const std::string& file_path);
    void clear_selection();
    void submit_task();
    void reset_state();

    bool can_submit() const;
    void draw_selected_files_chips(SDL_Renderer* renderer, int start_x, int start_y, int max_width) const;

    // UI state
    bool is_open_ = false;
    std::string search_term_;
    std::vector<std::string> filtered_files_;
    std::unordered_set<std::string> selected_files_;
    std::string task_text_;
    bool file_index_initialized_ = false;

    // UI widgets
    mutable std::unique_ptr<DMTextBox> search_box_;
    mutable std::unique_ptr<DMTextBox> task_box_;
    mutable std::unique_ptr<DMButton> submit_button_;
    mutable std::vector<std::unique_ptr<DMButton>> file_buttons_;

    // Positioning and layout
    mutable SDL_Rect popup_rect_;
    mutable SDL_Rect header_rect_;
    mutable SDL_Rect search_rect_;
    mutable SDL_Rect selection_rect_;
    mutable SDL_Rect task_rect_;
    mutable SDL_Rect buttons_rect_;

    // Scroll and display state
    mutable int file_scroll_offset_ = 0;
    mutable int max_visible_files_ = 10;
    mutable bool layout_dirty_ = true;

    // Dependencies
    std::unique_ptr<QuickTaskFileIndexer> file_indexer_;
    std::unique_ptr<QuickTaskCsvWriter> csv_writer_;
    devmode::core::ManifestStore* manifest_store_ = nullptr;
};
