#include "AnimationChildrenPanel.hpp"

#include <algorithm>
#include <cctype>
#include <optional>

#include "dev_mode/asset_sections/animation_editor_window/AnimationDocument.hpp"
#include "dev_mode/dm_styles.hpp"

namespace animation_editor {
namespace {
constexpr int kDefaultPanelWidth = 320;
constexpr int kDefaultPanelHeight = 180;
}

AnimationChildrenPanel::AnimationChildrenPanel()
    : DockableCollapsible("Children", true /*floatable*/, kDefaultPanelWidth, kDefaultPanelHeight) {
    set_show_header(true);

    child_dropdown_ = std::make_unique<DMDropdown>("Child", std::vector<std::string>{}, 0);
    child_dropdown_widget_ = std::make_unique<DropdownWidget>(child_dropdown_.get());

    name_box_ = std::make_unique<DMTextBox>("Name", "");
    name_widget_ = std::make_unique<TextBoxWidget>(name_box_.get(), false);

    add_button_ = std::make_unique<DMButton>("Add", &DMStyles::CreateButton(), 80, DMButton::height());
    add_widget_ = std::make_unique<ButtonWidget>(add_button_.get(), [this]() { this->add_child(); });

    rename_button_ = std::make_unique<DMButton>("Rename", &DMStyles::HeaderButton(), 100, DMButton::height());
    rename_widget_ = std::make_unique<ButtonWidget>(rename_button_.get(), [this]() { this->rename_child(); });

    remove_button_ = std::make_unique<DMButton>("Remove", &DMStyles::HeaderButton(), 100, DMButton::height());
    remove_widget_ = std::make_unique<ButtonWidget>(remove_button_.get(), [this]() { this->remove_child(); });

    last_name_value_ = name_box_->value();

    rebuild_rows();
}

void AnimationChildrenPanel::set_document(std::shared_ptr<AnimationDocument> document) {
    if (document_ == document) {
        return;
    }
    document_ = std::move(document);
    last_signature_.clear();
    sync_from_document();
}

void AnimationChildrenPanel::set_status_callback(std::function<void(const std::string&, int)> callback) {
    status_callback_ = std::move(callback);
}

void AnimationChildrenPanel::set_on_children_changed(std::function<void(const std::vector<std::string>&)> callback) {
    on_children_changed_ = std::move(callback);
}

void AnimationChildrenPanel::set_on_selection_changed(std::function<void(std::optional<std::string>)> callback) {
    on_selection_changed_ = std::move(callback);
}

std::optional<std::string> AnimationChildrenPanel::selected_child() const {
    if (selected_index_ < 0 || selected_index_ >= static_cast<int>(child_names_.size())) {
        return std::nullopt;
    }
    return child_names_[static_cast<std::size_t>(selected_index_)];
}

void AnimationChildrenPanel::refresh() {
    sync_from_document();
}

void AnimationChildrenPanel::update() {
    sync_from_document();
}

bool AnimationChildrenPanel::handle_event(const SDL_Event& e) {
    if (!is_visible()) return false;

    bool consumed = DockableCollapsible::handle_event(e);

    if (child_dropdown_ && !child_names_.empty()) {
        int current = child_dropdown_->selected();
        if (current != selected_index_) {
            select_child(current);
            consumed = true;
        }
    }

    if (name_box_) {
        const std::string now = name_box_->value();
        if (now != last_name_value_) {
            last_name_value_ = now;
            update_button_styles();
        }
    }

    notify_selection_changed();

    return consumed;
}

void AnimationChildrenPanel::set_work_area_bounds(const SDL_Rect& bounds) {
    set_work_area(bounds);
}

void AnimationChildrenPanel::rebuild_rows() {
    Rows rows;
    rows.push_back({child_dropdown_widget_.get()});
    rows.push_back({name_widget_.get()});
    rows.push_back({add_widget_.get(), rename_widget_.get(), remove_widget_.get()});
    set_rows(rows);
}

void AnimationChildrenPanel::sync_from_document() {
    const std::string signature = current_signature();
    if (signature == last_signature_) {
        update_button_styles();
        return;
    }
    last_signature_ = signature;

    child_names_ = sanitized_children_from_document();
    const bool has_children = !child_names_.empty();
    int next_index = selected_index_;
    if (has_children) {
        next_index = std::clamp(next_index, 0, static_cast<int>(child_names_.size()) - 1);
    } else {
        next_index = -1;
    }
    selected_index_ = next_index;

    std::vector<std::string> options = has_children ? child_names_ : std::vector<std::string>{"(no children)"};
    int dropdown_index = has_children ? selected_index_ : 0;
    child_dropdown_ = std::make_unique<DMDropdown>("Child", options, dropdown_index);
    child_dropdown_widget_ = std::make_unique<DropdownWidget>(child_dropdown_.get());

    if (name_box_ && !name_box_->is_editing()) {
        if (selected_index_ >= 0 && selected_index_ < static_cast<int>(child_names_.size())) {
            name_box_->set_value(child_names_[static_cast<std::size_t>(selected_index_)]);
        } else if (child_names_.empty()) {
            name_box_->set_value("");
        }
        last_name_value_ = name_box_->value();
    }

    rebuild_rows();
    update_button_styles();
    notify_selection_changed();
}

void AnimationChildrenPanel::update_button_styles() {
    const bool has_name = name_box_ && !trim_name(name_box_->value()).empty();
    const bool has_selection = selected_index_ >= 0 && selected_index_ < static_cast<int>(child_names_.size());

    const DMButtonStyle* add_style = has_name ? &DMStyles::AccentButton() : &DMStyles::HeaderButton();
    const DMButtonStyle* rename_style = (has_name && has_selection) ? &DMStyles::AccentButton() : &DMStyles::HeaderButton();
    const DMButtonStyle* remove_style = has_selection ? &DMStyles::AccentButton() : &DMStyles::HeaderButton();

    if (add_button_) add_button_->set_style(add_style);
    if (rename_button_) rename_button_->set_style(rename_style);
    if (remove_button_) remove_button_->set_style(remove_style);

}

void AnimationChildrenPanel::select_child(int index) {
    if (child_names_.empty()) {
        selected_index_ = -1;
        update_button_styles();
        notify_selection_changed();
        return;
    }
    index = std::clamp(index, 0, static_cast<int>(child_names_.size()) - 1);
    if (selected_index_ == index) {
        notify_selection_changed();
        return;
    }
    selected_index_ = index;
    if (name_box_ && !name_box_->is_editing()) {
        name_box_->set_value(child_names_[static_cast<std::size_t>(selected_index_)]);
        last_name_value_ = name_box_->value();
    }
    update_button_styles();
    notify_selection_changed();
}

void AnimationChildrenPanel::add_child() {
    if (!document_) {
        return;
    }
    std::string desired = trim_name(name_box_ ? name_box_->value() : std::string{});
    if (desired.empty()) {
        if (status_callback_) status_callback_("Child name cannot be empty.", 180);
        return;
    }
    auto it = std::find(child_names_.begin(), child_names_.end(), desired);
    if (it != child_names_.end()) {
        select_child(static_cast<int>(std::distance(child_names_.begin(), it)));
        if (status_callback_) status_callback_("Child already exists.", 120);
        return;
    }
    std::vector<std::string> next = child_names_;
    next.push_back(desired);
    apply_children(next, "Added child '" + desired + "'.");
    select_child(static_cast<int>(next.size()) - 1);
}

void AnimationChildrenPanel::rename_child() {
    if (!document_) {
        return;
    }
    if (selected_index_ < 0 || selected_index_ >= static_cast<int>(child_names_.size())) {
        if (status_callback_) status_callback_("Select a child to rename.", 150);
        return;
    }
    std::string desired = trim_name(name_box_ ? name_box_->value() : std::string{});
    if (desired.empty()) {
        if (status_callback_) status_callback_("Child name cannot be empty.", 180);
        return;
    }
    std::string current = child_names_[static_cast<std::size_t>(selected_index_)];
    if (desired == current) {
        if (status_callback_) status_callback_("Name unchanged.", 120);
        return;
    }
    auto it = std::find(child_names_.begin(), child_names_.end(), desired);
    if (it != child_names_.end()) {
        select_child(static_cast<int>(std::distance(child_names_.begin(), it)));
        if (status_callback_) status_callback_("Name already in use.", 150);
        return;
    }
    std::vector<std::string> next = child_names_;
    next[static_cast<std::size_t>(selected_index_)] = desired;
    apply_children(next, "Renamed child to '" + desired + "'.");
    select_child(selected_index_);
}

void AnimationChildrenPanel::remove_child() {
    if (!document_) {
        return;
    }
    if (selected_index_ < 0 || selected_index_ >= static_cast<int>(child_names_.size())) {
        if (status_callback_) status_callback_("Select a child to remove.", 150);
        return;
    }
    std::vector<std::string> next;
    next.reserve(child_names_.size() > 0 ? child_names_.size() - 1 : 0);
    for (std::size_t i = 0; i < child_names_.size(); ++i) {
        if (static_cast<int>(i) == selected_index_) continue;
        next.push_back(child_names_[i]);
    }
    const std::string removed = child_names_[static_cast<std::size_t>(selected_index_)];
    apply_children(next, "Removed child '" + removed + "'.");
    if (next.empty()) {
        select_child(-1);
    } else {
        select_child(std::min(selected_index_, static_cast<int>(next.size()) - 1));
    }
}

std::vector<std::string> AnimationChildrenPanel::sanitized_children_from_document() const {
    if (!document_) {
        return {};
    }
    return document_->animation_children();
}

std::string AnimationChildrenPanel::current_signature() const {
    if (!document_) {
        return std::string{};
    }
    return document_->animation_children_signature();
}

std::string AnimationChildrenPanel::trim_name(const std::string& raw) {
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    std::string s = raw;
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

void AnimationChildrenPanel::apply_children(const std::vector<std::string>& next, const std::string& status_message) {
    if (!document_) {
        return;
    }
    document_->replace_animation_children(next);
    last_signature_.clear();
    sync_from_document();
    if (on_children_changed_) {
        on_children_changed_(next);
    }
    if (status_callback_) {
        status_callback_(status_message, 210);
    }
}

void AnimationChildrenPanel::notify_selection_changed() {
    if (!on_selection_changed_) {
        return;
    }
    on_selection_changed_(selected_child());
}

} // namespace animation_editor