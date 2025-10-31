#include "quick_task_popup.hpp"
#include "dev_mode/widgets.hpp"
#include "dev_mode/dm_styles.hpp"
#include "dev_mode/core/manifest_store.hpp"
#include <SDL.h>
#include <algorithm>
#include <memory>

QuickTaskPopup::QuickTaskPopup()
    : file_indexer_(std::make_unique<QuickTaskFileIndexer>()),
      csv_writer_(std::make_unique<QuickTaskCsvWriter>()) {
}

QuickTaskPopup::~QuickTaskPopup() = default;

void QuickTaskPopup::set_manifest_store(devmode::core::ManifestStore* store) {
    manifest_store_ = store;
}

void QuickTaskPopup::open() {
    if (is_open_) return;

    is_open_ = true;
    file_scroll_offset_ = 0;
    layout_dirty_ = true;

    // Initialize file index if needed
    if (!file_index_initialized_) {
        file_indexer_->index_repo_files();
        file_index_initialized_ = true;
    }

    update_filtered_files();
    rebuild_ui();
}

void QuickTaskPopup::close() {
    if (!is_open_) return;
    is_open_ = false;
    reset_state();
}

void QuickTaskPopup::handle_escape() {
    close();
}

void QuickTaskPopup::update() {
    // Nothing to animate for now
}

void QuickTaskPopup::render(SDL_Renderer* renderer) const {
    if (!is_open_) return;

    // Get screen size for centering
    int screen_w, screen_h;
    SDL_GetRendererOutputSize(renderer, &screen_w, &screen_h);
    SDL_Rect screen_rect = {0, 0, screen_w, screen_h};

    // Layout if needed
    if (layout_dirty_ || popup_rect_.w == 0) {
        layout_ui(screen_rect);
        layout_dirty_ = false;
    }

    // Draw modal overlay (semi-transparent black background)
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
    SDL_RenderFillRect(renderer, &screen_rect);

    // Draw popup background
    SDL_SetRenderDrawColor(renderer, 40, 40, 45, 255);
    SDL_RenderFillRect(renderer, &popup_rect_);

    // Draw border
    SDL_SetRenderDrawColor(renderer, 80, 80, 100, 255);
    SDL_RenderDrawRect(renderer, &popup_rect_);

    // Draw header
    const DMLabelStyle& header_style = DMStyles::Label();
    int title_x = header_rect_.x + 20;
    int title_y = header_rect_.y + (header_rect_.h - 16) / 2; // Approximate font height
    SDL_Color title_color = {255, 255, 255, 255};
    // TODO: Implement text rendering

    // Draw search section header
    int search_label_x = search_rect_.x + 10;
    int search_label_y = search_rect_.y + 5;
    const DMLabelStyle& small_label_style = DMStyles::Label();
    SDL_Color label_color = {200, 200, 220, 255};
    // TODO: Implement text rendering

    // Draw task section header
    int task_label_x = task_rect_.x + 10;
    int task_label_y = task_rect_.y + 5;
    // TODO: Implement text rendering

    // Render UI widgets
    if (search_box_) search_box_->render(renderer);
    if (task_box_) task_box_->render(renderer);

    // Draw selected files chips
    if (!selected_files_.empty()) {
        draw_selected_files_chips(renderer, search_rect_.x + 10, search_rect_.y + 45, search_rect_.w - 20);
    }

    // Draw file list buttons
    int file_y = selection_rect_.y + 10;
    for (size_t i = file_scroll_offset_; i < file_buttons_.size() && i < file_scroll_offset_ + max_visible_files_; ++i) {
        if (i < file_buttons_.size() && file_buttons_[i]) {
            file_buttons_[i]->render(renderer);
        }
    }

    if (submit_button_) {
        submit_button_->render(renderer);
    }

    // Note: Could add submission info text here if needed
}

bool QuickTaskPopup::handle_event(const SDL_Event& event) {
    if (!is_open_) return false;

    bool consumed = false;

    // Handle escape key
    if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
        handle_escape();
        consumed = true;
    }

    // Handle search box
    if (search_box_ && search_box_->handle_event(event)) {
        consumed = true;
        search_term_ = search_box_->value();
        update_filtered_files();
    }

    // Handle task box
    if (task_box_ && task_box_->handle_event(event)) {
        consumed = true;
        task_text_ = task_box_->value();
    }

    // Handle file selection buttons
    for (size_t i = 0; i < file_buttons_.size(); ++i) {
        if (file_buttons_[i] && file_buttons_[i]->handle_event(event)) {
            consumed = true;
            if (i < filtered_files_.size()) {
                const std::string& file_path = filtered_files_[i];
                if (selected_files_.find(file_path) != selected_files_.end()) {
                    deselect_file(file_path);
                } else {
                    select_file(file_path);
                }
                rebuild_ui(); // Redraw to show selection changes
            }
            break;
        }
    }

    // Handle submit button
    if (submit_button_ && submit_button_->handle_event(event) && event.type == SDL_MOUSEBUTTONUP) {
        consumed = true;
        submit_task();
    }

    return consumed;
}

void QuickTaskPopup::rebuild_ui() {
    // Clear existing widgets
    search_box_.reset();
    task_box_.reset();
    submit_button_.reset();
    file_buttons_.clear();

    // Create search box
    search_box_ = std::make_unique<DMTextBox>("Search files", search_term_);

    // Create task box
    task_box_ = std::make_unique<DMTextBox>("Task description", task_text_);

    // Create file buttons
    for (size_t i = 0; i < filtered_files_.size() && i < static_cast<size_t>(max_visible_files_); ++i) {
        std::string btn_text = filtered_files_[i];
        auto button = std::make_unique<DMButton>(btn_text, nullptr, 100, 24);
        if (selected_files_.find(filtered_files_[i]) != selected_files_.end()) {
            // Mark as selected
            btn_text = "[+] " + btn_text;
            button = std::make_unique<DMButton>(btn_text, nullptr, 100, 24);
        }
        file_buttons_.push_back(std::move(button));
    }

    // Create submit button
    submit_button_ = std::make_unique<DMButton>("Submit Task", nullptr, 120, 32);

    // Mark layout as dirty
    layout_dirty_ = true;
}

void QuickTaskPopup::layout_ui(const SDL_Rect& screen_rect) const {
    int popup_width = 600;
    int popup_height = 500;

    popup_rect_ = {
        screen_rect.x + (screen_rect.w - popup_width) / 2,
        screen_rect.y + (screen_rect.h - popup_height) / 2,
        popup_width,
        popup_height
    };

    // Header section
    header_rect_ = {popup_rect_.x, popup_rect_.y, popup_width, 40};

    // Search section
    search_rect_ = {popup_rect_.x + 10, header_rect_.y + header_rect_.h, popup_width - 20, 120};
    if (search_box_) {
        SDL_Rect search_box_rect = {search_rect_.x + 10, search_rect_.y + 25, search_rect_.w - 20, 32};
        search_box_->set_rect(search_box_rect);
    }

    // Selection section (below search)
    selection_rect_ = {popup_rect_.x + 10, search_rect_.y + search_rect_.h, popup_width - 20, 180};
    int button_y = selection_rect_.y + 10;
    for (size_t i = 0; i < file_buttons_.size(); ++i) {
        if (file_buttons_[i]) {
            SDL_Rect button_rect = {selection_rect_.x + 10, button_y, selection_rect_.w - 20, 24};
            file_buttons_[i]->set_rect(button_rect);
            button_y += 26;
        }
    }

    // Task section
    task_rect_ = {popup_rect_.x + 10, selection_rect_.y + selection_rect_.h, popup_width - 20, 100};
    if (task_box_) {
        SDL_Rect task_box_rect = {task_rect_.x + 10, task_rect_.y + 25, task_rect_.w - 20, 60};
        task_box_->set_rect(task_box_rect);
    }

    // Buttons section
    buttons_rect_ = {popup_rect_.x + 10, task_rect_.y + task_rect_.h, popup_width - 20, 40};
    if (submit_button_) {
        SDL_Rect submit_rect = {buttons_rect_.x + buttons_rect_.w - 130, buttons_rect_.y + 4, 120, 32};
        submit_button_->set_rect(submit_rect);
    }
}

void QuickTaskPopup::update_filtered_files() {
    filtered_files_ = file_indexer_->filter_files(search_term_);
    rebuild_ui();
}

void QuickTaskPopup::select_file(const std::string& file_path) {
    selected_files_.insert(file_path);
}

void QuickTaskPopup::deselect_file(const std::string& file_path) {
    selected_files_.erase(file_path);
}

void QuickTaskPopup::clear_selection() {
    selected_files_.clear();
}

void QuickTaskPopup::submit_task() {
    if (!can_submit()) return;

    QuickTaskEntry entry;
    entry.completed = false;
    entry.prompt_fixed = false;
    entry.related_files.assign(selected_files_.begin(), selected_files_.end());
    entry.original_prompt = task_text_;
    entry.ai_fixed_prompt = "";

    if (csv_writer_->add_task_entry(entry)) {
        reset_state();
        close();
    }
}

void QuickTaskPopup::reset_state() {
    search_term_.clear();
    task_text_.clear();
    clear_selection();
    file_scroll_offset_ = 0;
}

bool QuickTaskPopup::can_submit() const {
    return !task_text_.empty();  // Allow submission without files for now
}

void QuickTaskPopup::draw_selected_files_chips(SDL_Renderer* renderer, int start_x, int start_y, int max_width) const {
    int current_x = start_x;
    int current_y = start_y;
    int chip_height = 20;

    for (const auto& file_path : selected_files_) {
        std::string chip_text = "x " + file_path;
        // Estimate text width (simplified for now - ~8 chars per 100 pixels)
        int estimated_text_width = static_cast<int>(chip_text.length() * 10);
        int chip_width = std::min(estimated_text_width + 8, 200); // Max width per chip

        // Check if chip fits on current line
        if (current_x + chip_width > start_x + max_width && current_x > start_x) {
            // Move to next line
            current_x = start_x;
            current_y += chip_height + 2;
        }

        // Draw chip background
        SDL_Rect chip_rect = {current_x, current_y, chip_width, chip_height};
        SDL_SetRenderDrawColor(renderer, 60, 60, 80, 255);
        SDL_RenderFillRect(renderer, &chip_rect);
        SDL_SetRenderDrawColor(renderer, 100, 100, 120, 255);
        SDL_RenderDrawRect(renderer, &chip_rect);

        // Note: Text rendering removed for now

        current_x += chip_width + 4;
    }
}
