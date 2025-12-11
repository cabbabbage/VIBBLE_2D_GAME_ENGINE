#include "quick_task_popup.hpp"
#include "dev_mode/widgets.hpp"
#include "dev_mode/dm_styles.hpp"
#include "dev_mode/font_cache.hpp"
#include <SDL.h>
#include <algorithm>

QuickTaskPopup::QuickTaskPopup() = default;

QuickTaskPopup::~QuickTaskPopup() = default;

void QuickTaskPopup::set_manifest_store(devmode::core::ManifestStore* store) {
    manifest_store_ = store;
}

void QuickTaskPopup::open() {
    if (is_open_) return;
    is_open_ = true;
    layout_dirty_ = true;

    dev_file_.ensure_initialized();
    cline_file_.ensure_initialized();
    dev_file_.load(dev_tasks_);
    cline_file_.load(cline_tasks_);

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

    if (assignee_dd_) assignee_dd_->render(renderer);
    if (assigner_dd_) assigner_dd_->render(renderer);
    if (description_box_) description_box_->render(renderer);
    if (add_button_)   add_button_->render(renderer);

    if (dev_label_) dev_label_->render(renderer);
    if (cline_label_) cline_label_->render(renderer);

    const DMLabelStyle& ls = DMStyles::Label();
    int row_y = dev_rect_.y + DMButton::height() + 6;
    const int row_h = DMTextBox::height();
    const int pad = 6;
    for (size_t i = 0; i < dev_tasks_.size(); ++i) {
        const int x0 = dev_rect_.x + pad;
        DrawLabelText(renderer, dev_tasks_[i].description, x0, row_y + (row_h - ls.font_size)/2, ls);
        if (i < dev_row_widgets_.size() && dev_row_widgets_[i].delete_button) {
            dev_row_widgets_[i].delete_button->render(renderer);
        }
        row_y += row_h + 6;
        if (row_y > dev_rect_.y + dev_rect_.h - row_h) break;
    }

    row_y = cline_rect_.y + DMButton::height() + 6;
    for (size_t i = 0; i < cline_tasks_.size(); ++i) {
        const int x0 = cline_rect_.x + pad;
        DrawLabelText(renderer, cline_tasks_[i].description, x0, row_y + (row_h - ls.font_size)/2, ls);
        if (i < cline_row_widgets_.size() && cline_row_widgets_[i].delete_button) {
            cline_row_widgets_[i].delete_button->render(renderer);
        }
        row_y += row_h + 6;
        if (row_y > cline_rect_.y + cline_rect_.h - row_h) break;
    }

    DMDropdown::render_active_options(renderer);
}

bool QuickTaskPopup::handle_event(const SDL_Event& event) {
    if (!is_open_) return false;

    if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
        handle_escape();
        return true;
    }

    bool consumed = false;

    if (assignee_dd_ && assignee_dd_->handle_event(event)) consumed = true;
    if (assigner_dd_ && assigner_dd_->handle_event(event)) consumed = true;
    if (description_box_ && description_box_->handle_event(event)) consumed = true;
    if (add_button_ && add_button_->handle_event(event) && event.type == SDL_MOUSEBUTTONUP) {
        consumed = true;
        add_new_task();
    }

    for (size_t i = 0; i < dev_row_widgets_.size(); ++i) {
        auto& rw = dev_row_widgets_[i];
        if (i >= dev_tasks_.size()) break;
        if (rw.delete_button && rw.delete_button->handle_event(event) && event.type == SDL_MOUSEBUTTONUP) {
            delete_dev_task(i);
            consumed = true;
            break;
        }
    }
    for (size_t i = 0; i < cline_row_widgets_.size(); ++i) {
        auto& rw = cline_row_widgets_[i];
        if (i >= cline_tasks_.size()) break;
        if (rw.delete_button && rw.delete_button->handle_event(event) && event.type == SDL_MOUSEBUTTONUP) {
            delete_cline_task(i);
            consumed = true;
            break;
        }
    }

    if (event.type == SDL_MOUSEMOTION || event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP || event.type == SDL_MOUSEWHEEL) {
        return true;
    }
    return consumed;
}

void QuickTaskPopup::rebuild_ui() {

    const std::vector<std::string> assignee_opts{ "Any", "Cal", "Kaden", "Haden", "Cline" };
    const std::vector<std::string> assigner_opts{ "Cal", "Kaden", "Haden" };
    int default_assignee = assignee_dd_ ? assignee_dd_->selected() : 0;
    int default_assigner = assigner_dd_ ? assigner_dd_->selected() : 0;
    assignee_dd_ = std::make_unique<DMDropdown>("Assignee", assignee_opts, std::clamp(default_assignee, 0, (int)assignee_opts.size()-1));
    assigner_dd_ = std::make_unique<DMDropdown>("Assigner", assigner_opts, std::clamp(default_assigner, 0, (int)assigner_opts.size()-1));
    description_box_ = std::make_unique<DMTextBox>("Description", description_box_ ? description_box_->value() : std::string());
    add_button_   = std::make_unique<DMButton>("Add Task", &DMStyles::CreateButton(), 100, DMButton::height());

    dev_label_ = std::make_unique<DMButton>("Dev Tasks", &DMStyles::HeaderButton(), 0, DMButton::height());
    cline_label_ = std::make_unique<DMButton>("Cline Tasks", &DMStyles::HeaderButton(), 0, DMButton::height());

    dev_row_widgets_.clear();
    dev_row_widgets_.resize(dev_tasks_.size());
    for (size_t i = 0; i < dev_tasks_.size(); ++i) {
        dev_row_widgets_[i].delete_button = std::make_unique<DMButton>("x", &DMStyles::DeleteButton(), DMButton::height(), DMButton::height());
    }
    cline_row_widgets_.clear();
    cline_row_widgets_.resize(cline_tasks_.size());
    for (size_t i = 0; i < cline_tasks_.size(); ++i) {
        cline_row_widgets_[i].delete_button = std::make_unique<DMButton>("x", &DMStyles::DeleteButton(), DMButton::height(), DMButton::height());
    }

    layout_dirty_ = true;
}

void QuickTaskPopup::layout_ui(const SDL_Rect& screen_rect) const {
    const int popup_width = std::min(1200, screen_rect.w - 80);
    const int popup_height = std::min(700, screen_rect.h - 80);
    popup_rect_ = { screen_rect.x + (screen_rect.w - popup_width)/2,
                    screen_rect.y + (screen_rect.h - popup_height)/2, popup_width, popup_height };

    const int topbar_x = popup_rect_.x + 12;
    const int topbar_y = popup_rect_.y + 12;
    const int topbar_w = popup_rect_.w - 24;

    const int gap = 10;
    const int assignee_w = 180;
    const int assigner_w = 160;
    const int add_w = 110;
    int desc_w = topbar_w - (assignee_w + gap + assigner_w + gap + add_w + gap);
    if (desc_w < 200) desc_w = 200;

    int assignee_h = assignee_dd_ ? assignee_dd_->preferred_height(assignee_w) : DMButton::height();
    int assigner_h = assigner_dd_ ? assigner_dd_->preferred_height(assigner_w) : DMButton::height();
    int desc_h = description_box_ ? description_box_->height_for_width(desc_w) : DMTextBox::height();
    int add_h = DMButton::height();
    const int topbar_h = std::max({assignee_h, assigner_h, desc_h, add_h});

    topbar_rect_ = { topbar_x, topbar_y, topbar_w, topbar_h };

    int x = topbar_rect_.x;
    const int y = topbar_rect_.y;

    if (assignee_dd_) {
        assignee_dd_->set_rect(SDL_Rect{ x, y, assignee_w, topbar_h });
        x += assignee_w + gap;
    }
    if (assigner_dd_) {
        assigner_dd_->set_rect(SDL_Rect{ x, y, assigner_w, topbar_h });
        x += assigner_w + gap;
    }
    if (description_box_) {
        description_box_->set_rect(SDL_Rect{ x, y, desc_w, topbar_h });
        x += desc_w + gap;
    }
    if (add_button_) {
        add_button_->set_rect(SDL_Rect{ x, y + (topbar_h - add_h) / 2, add_w, add_h });
    }

    lists_rect_ = { popup_rect_.x + 12, topbar_rect_.y + topbar_rect_.h + 12, popup_rect_.w - 24, popup_rect_.h - (topbar_rect_.h + 24) };
    const int col_gap = 8;
    int col_w = (lists_rect_.w - col_gap) / 2;
    dev_rect_ = { lists_rect_.x, lists_rect_.y, col_w, lists_rect_.h };
    cline_rect_ = { lists_rect_.x + col_w + col_gap, lists_rect_.y, col_w, lists_rect_.h };

    if (dev_label_) dev_label_->set_rect(SDL_Rect{ dev_rect_.x, dev_rect_.y, dev_rect_.w, DMButton::height() });
    if (cline_label_) cline_label_->set_rect(SDL_Rect{ cline_rect_.x, cline_rect_.y, cline_rect_.w, DMButton::height() });

    auto place_rows = [&](const std::vector<SimpleTask>& tasks, std::vector<RowWidgets>& rows, const SDL_Rect& rect) {
        int y_cursor = rect.y + DMButton::height() + 6;
        for (size_t i = 0; i < tasks.size() && i < rows.size(); ++i) {
            int x_delete = rect.x + rect.w - DMButton::height() - 6;
            if (rows[i].delete_button) {
                rows[i].delete_button->set_rect(SDL_Rect{ x_delete, y_cursor, DMButton::height(), DMButton::height() });
            }
            y_cursor += DMTextBox::height() + 6;
            if (y_cursor > rect.y + rect.h - DMTextBox::height()) break;
        }
};
    place_rows(dev_tasks_, dev_row_widgets_, dev_rect_);
    place_rows(cline_tasks_, cline_row_widgets_, cline_rect_);
}

void QuickTaskPopup::add_new_task() {
    if (!description_box_) return;
    const std::string desc = description_box_->value();
    if (desc.empty()) return;

    const std::vector<std::string> assignee_opts{ "Any", "Cal", "Kaden", "Haden", "Cline" };
    const std::vector<std::string> assigner_opts{ "Cal", "Kaden", "Haden" };

    const int assignee_idx = assignee_dd_ ? assignee_dd_->selected() : 0;
    const int assigner_idx = assigner_dd_ ? assigner_dd_->selected() : 0;
    const std::string assignee = assignee_opts[std::clamp(assignee_idx, 0, (int)assignee_opts.size()-1)];
    const std::string assigner = assigner_opts[std::clamp(assigner_idx, 0, (int)assigner_opts.size()-1)];

    SimpleTask t;
    t.description = desc;
    t.assignee = assignee;
    t.assigner = assigner;
    t.status = "pending";

    if (assignee == "Cline") {
        cline_tasks_.insert(cline_tasks_.begin(), std::move(t));
    } else {
        dev_tasks_.insert(dev_tasks_.begin(), std::move(t));
    }
    persist_all();
    description_box_->set_value("");
}

void QuickTaskPopup::delete_dev_task(size_t index) {
    if (index >= dev_tasks_.size()) return;
    dev_tasks_.erase(dev_tasks_.begin() + index);
    persist_all();
}

void QuickTaskPopup::delete_cline_task(size_t index) {
    if (index >= cline_tasks_.size()) return;
    cline_tasks_.erase(cline_tasks_.begin() + index);
    persist_all();
}

void QuickTaskPopup::persist_all() {
    dev_file_.save(dev_tasks_);
    cline_file_.save(cline_tasks_);
    rebuild_ui();
}
