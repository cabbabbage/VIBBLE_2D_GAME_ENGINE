#include "quick_task_popup.hpp"
#include "dev_mode/widgets.hpp"
#include "dev_mode/dm_styles.hpp"
#include "dev_mode/core/manifest_store.hpp"
#include <SDL.h>
#include <algorithm>
#include <memory>

namespace {
const char* status_label(DevTaskStatus s) {
    switch (s) {
        case DevTaskStatus::PendingClineDescription: return "Cline Description";
        case DevTaskStatus::PendingFixVerification:  return "Fix Verification";
        case DevTaskStatus::Completed:               return "Completed";
    }
    return "";
}
}

QuickTaskPopup::QuickTaskPopup()
    : file_indexer_(std::make_unique<QuickTaskFileIndexer>()) {
}

QuickTaskPopup::~QuickTaskPopup() = default;

void QuickTaskPopup::set_manifest_store(devmode::core::ManifestStore* store) {
    manifest_store_ = store;
}

void QuickTaskPopup::open() {
    if (is_open_) return;
    is_open_ = true;
    layout_dirty_ = true;

    // Ensure DEV_TASKS.md exists and load tasks
    tasks_io_.ensure_initialized();
    tasks_io_.load(tasks_);
    group_tasks_by_lane();

    // Initialize file index once
    file_indexer_->index_repo_files();
    rebuild_ui();
}

void QuickTaskPopup::close() {
    if (!is_open_) return;
    is_open_ = false;
}

void QuickTaskPopup::handle_escape() {
    close();
}

void QuickTaskPopup::update() {
    // No animations
}

void QuickTaskPopup::render(SDL_Renderer* renderer) {
    if (!is_open_) return;

    int screen_w, screen_h;
    SDL_GetRendererOutputSize(renderer, &screen_w, &screen_h);
    SDL_Rect screen_rect = {0, 0, screen_w, screen_h};

    if (layout_dirty_ || popup_rect_.w == 0) {
        layout_ui(screen_rect);
        layout_dirty_ = false;
    }

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
    SDL_RenderFillRect(renderer, &screen_rect);

    SDL_SetRenderDrawColor(renderer, 40, 40, 45, 255);
    SDL_RenderFillRect(renderer, &popup_rect_);
    SDL_SetRenderDrawColor(renderer, 80, 80, 100, 255);
    SDL_RenderDrawRect(renderer, &popup_rect_);

    // Render top bar widgets
    if (assignee_box_) assignee_box_->render(renderer);
    if (new_task_box_) new_task_box_->render(renderer);
    if (add_button_)   add_button_->render(renderer);
    if (show_completed_checkbox_) show_completed_checkbox_->render(renderer);

    // Lane headers
    if (lane1_label_) lane1_label_->render(renderer);
    if (lane2_label_) lane2_label_->render(renderer);
    if (show_completed_lane_ && lane3_label_) lane3_label_->render(renderer);

    // Render per-task widgets
    for (const auto& tw : task_widgets_) {
        if (tw.checkbox)      tw.checkbox->render(renderer);
        if (tw.title_edit)    tw.title_edit->render(renderer);
        if (tw.status_button) tw.status_button->render(renderer);
        if (tw.delete_button) tw.delete_button->render(renderer);
    }
}

bool QuickTaskPopup::handle_event(const SDL_Event& event) {
    if (!is_open_) return false;
    bool consumed = false;

    if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
        handle_escape();
        return true;
    }

    if (assignee_box_ && assignee_box_->handle_event(event)) consumed = true;
    if (new_task_box_ && new_task_box_->handle_event(event)) consumed = true;
    if (show_completed_checkbox_ && show_completed_checkbox_->handle_event(event)) {
        consumed = true;
        show_completed_lane_ = show_completed_checkbox_->value();
        rebuild_ui();
    }
    if (add_button_ && add_button_->handle_event(event) && event.type == SDL_MOUSEBUTTONUP) {
        consumed = true;
        add_new_task();
    }

    // Handle per-task widgets
    for (size_t i = 0; i < task_widgets_.size(); ++i) {
        auto& tw = task_widgets_[i];
        const bool valid = (i < tasks_.size());
        if (!valid) break;
        if (tw.checkbox && tw.checkbox->handle_event(event)) {
            consumed = true;
            toggle_task_completed(i, tw.checkbox->value());
            break;
        }
        if (tw.title_edit && tw.title_edit->handle_event(event)) {
            consumed = true;
            tasks_[i].title = tw.title_edit->value();
            tasks_[i].cline_description = tasks_[i].title; // keep in sync
            persist();
            break;
        }
        if (tw.status_button && tw.status_button->handle_event(event) && event.type == SDL_MOUSEBUTTONUP) {
            consumed = true;
            // Cycle status
            DevTaskStatus s = tasks_[i].status;
            if (s == DevTaskStatus::PendingClineDescription) s = DevTaskStatus::PendingFixVerification;
            else if (s == DevTaskStatus::PendingFixVerification) s = DevTaskStatus::Completed;
            else s = DevTaskStatus::PendingClineDescription;
            set_task_status(i, s);
            break;
        }
        if (tw.delete_button && tw.delete_button->handle_event(event) && event.type == SDL_MOUSEBUTTONUP) {
            consumed = true;
            delete_task(i);
            break;
        }
    }

    return consumed;
}

void QuickTaskPopup::group_tasks_by_lane() {
    lane1_indices_.clear();
    lane2_indices_.clear();
    lane3_indices_.clear();
    for (size_t i = 0; i < tasks_.size(); ++i) {
        switch (tasks_[i].status) {
            case DevTaskStatus::PendingClineDescription: lane1_indices_.push_back(i); break;
            case DevTaskStatus::PendingFixVerification:  lane2_indices_.push_back(i); break;
            case DevTaskStatus::Completed:               lane3_indices_.push_back(i); break;
        }
    }
}

void QuickTaskPopup::rebuild_ui() {
    // Top bar
    assignee_box_ = std::make_unique<DMTextBox>("@ Assignee (blank = CLINE)", assignee_box_ ? assignee_box_->value() : std::string());
    new_task_box_ = std::make_unique<DMTextBox>("New task", new_task_box_ ? new_task_box_->value() : std::string());
    add_button_   = std::make_unique<DMButton>("Add", &DMStyles::CreateButton(), 80, DMButton::height());
    show_completed_checkbox_ = std::make_unique<DMCheckbox>("Show Completed", show_completed_lane_);

    // Lane labels
    lane1_label_ = std::make_unique<DMButton>("Pending — Cline Description", &DMStyles::HeaderButton(), 0, DMButton::height());
    lane2_label_ = std::make_unique<DMButton>("Pending — Fix Verification", &DMStyles::HeaderButton(), 0, DMButton::height());
    lane3_label_ = std::make_unique<DMButton>("Completed", &DMStyles::HeaderButton(), 0, DMButton::height());

    // Per-task widgets, one array aligned to tasks_
    task_widgets_.clear();
    task_widgets_.resize(tasks_.size());
    for (size_t i = 0; i < tasks_.size(); ++i) {
        // Checkbox
        bool checked = (tasks_[i].status == DevTaskStatus::Completed);
        task_widgets_[i].checkbox = std::make_unique<DMCheckbox>("", checked);
        // Title edit
        task_widgets_[i].title_edit = std::make_unique<DMTextBox>("", tasks_[i].title);
        // Status button
        task_widgets_[i].status_button = std::make_unique<DMButton>(status_label(tasks_[i].status), &DMStyles::ListButton(), 140, DMButton::height());
        // Delete button (×)
        task_widgets_[i].delete_button = std::make_unique<DMButton>("×", &DMStyles::DeleteButton(), DMButton::height(), DMButton::height());
    }

    layout_dirty_ = true;
}

void QuickTaskPopup::layout_ui(const SDL_Rect& screen_rect) const {
    const int popup_width = std::min(1200, screen_rect.w - 80);
    const int popup_height = std::min(700, screen_rect.h - 80);
    popup_rect_ = { screen_rect.x + (screen_rect.w - popup_width)/2,
                    screen_rect.y + (screen_rect.h - popup_height)/2,
                    popup_width, popup_height };

    // Top bar
    topbar_rect_ = { popup_rect_.x + 12, popup_rect_.y + 12, popup_rect_.w - 24, 40 };
    const int ctl_h = DMTextBox::height();
    int x = topbar_rect_.x;
    const int y = topbar_rect_.y;

    if (assignee_box_) {
        assignee_box_->set_rect(SDL_Rect{ x, y, 220, ctl_h });
        x += 230;
    }
    if (new_task_box_) {
        int w = topbar_rect_.w - 230 - 80 - 160; // leave space for Add + Show Completed
        if (w < 200) w = 200;
        new_task_box_->set_rect(SDL_Rect{ x, y, w, ctl_h });
        x += w + 10;
    }
    if (add_button_) {
        add_button_->set_rect(SDL_Rect{ x, y, 80, DMButton::height() });
        x += 90;
    }
    if (show_completed_checkbox_) {
        show_completed_checkbox_->set_rect(SDL_Rect{ topbar_rect_.x + topbar_rect_.w - 160, y, 160, DMCheckbox::height() });
    }

    // Lanes area
    lanes_rect_ = { popup_rect_.x + 12, topbar_rect_.y + topbar_rect_.h + 12, popup_rect_.w - 24, popup_rect_.h - (topbar_rect_.h + 24) };
    const int col_gap = 8;
    int visible_cols = show_completed_lane_ ? 3 : 2;
    int col_w = (lanes_rect_.w - col_gap * (visible_cols - 1)) / visible_cols;
    lane1_rect_ = { lanes_rect_.x, lanes_rect_.y, col_w, lanes_rect_.h };
    lane2_rect_ = { lanes_rect_.x + col_w + col_gap, lanes_rect_.y, col_w, lanes_rect_.h };
    if (show_completed_lane_) {
        lane3_rect_ = { lanes_rect_.x + (col_w + col_gap) * 2, lanes_rect_.y, col_w, lanes_rect_.h };
    } else {
        lane3_rect_ = {0,0,0,0};
    }

    // Lane headers as buttons at the top of each column
    if (lane1_label_) lane1_label_->set_rect(SDL_Rect{ lane1_rect_.x, lane1_rect_.y, lane1_rect_.w, DMButton::height() });
    if (lane2_label_) lane2_label_->set_rect(SDL_Rect{ lane2_rect_.x, lane2_rect_.y, lane2_rect_.w, DMButton::height() });
    if (show_completed_lane_ && lane3_label_) lane3_label_->set_rect(SDL_Rect{ lane3_rect_.x, lane3_rect_.y, lane3_rect_.w, DMButton::height() });

    // Position per-task widgets within their lanes
    auto place_lane = [&](const std::vector<size_t>& indices, const SDL_Rect& rect, int& out_y_cursor) {
        int y_cursor = rect.y + DMButton::height() + 6;
        for (size_t idx : indices) {
            if (idx >= task_widgets_.size()) continue;
            auto& tw = task_widgets_[idx];
            int x0 = rect.x + 6;
            // Checkbox
            if (tw.checkbox) {
                tw.checkbox->set_rect(SDL_Rect{ x0, y_cursor, 20, DMCheckbox::height() });
            }
            int x1 = x0 + 24;
            int title_w = rect.w - 24 - 150 - 28 - 12; // status + delete spacing
            if (title_w < 60) title_w = 60;
            if (tw.title_edit) {
                tw.title_edit->set_rect(SDL_Rect{ x1, y_cursor, title_w, DMTextBox::height() });
            }
            int x2 = x1 + title_w + 6;
            if (tw.status_button) {
                tw.status_button->set_rect(SDL_Rect{ x2, y_cursor, 140, DMButton::height() });
            }
            int x3 = x2 + 146;
            if (tw.delete_button) {
                tw.delete_button->set_rect(SDL_Rect{ x3, y_cursor, DMButton::height(), DMButton::height() });
            }
            y_cursor += DMTextBox::height() + 6;
        }
        out_y_cursor = y_cursor;
    };

    int dummy;
    place_lane(lane1_indices_, lane1_rect_, dummy);
    place_lane(lane2_indices_, lane2_rect_, dummy);
    if (show_completed_lane_) place_lane(lane3_indices_, lane3_rect_, dummy);
}

void QuickTaskPopup::add_new_task() {
    if (!new_task_box_) return;
    const std::string title = new_task_box_->value();
    if (title.empty()) return;

    DevTask t;
    t.title = title;
    t.cline_description = title;
    t.status = DevTaskStatus::PendingClineDescription;
    t.assignee = assignee_box_ ? assignee_box_->value() : std::string();

    // Compute ID and created date
    const std::string new_id = tasks_io_.next_id_for_today(tasks_);
    t.id = new_id;
    t.created = (new_id.size() >= 13) ? new_id.substr(2, 10) : std::string();

    tasks_.insert(tasks_.begin(), std::move(t));
    persist();
    new_task_box_->set_value("");
}

void QuickTaskPopup::delete_task(size_t index) {
    if (index >= tasks_.size()) return;
    tasks_.erase(tasks_.begin() + index);
    persist();
}

void QuickTaskPopup::set_task_status(size_t index, DevTaskStatus status) {
    if (index >= tasks_.size()) return;
    tasks_[index].status = status;
    persist();
}

void QuickTaskPopup::toggle_task_completed(size_t index, bool completed) {
    if (index >= tasks_.size()) return;
    tasks_[index].status = completed ? DevTaskStatus::Completed : DevTaskStatus::PendingClineDescription;
    persist();
}

void QuickTaskPopup::persist() {
    // Save to markdown
    tasks_io_.save(tasks_);
    // Re-group and rebuild UI
    group_tasks_by_lane();
    rebuild_ui();
}
