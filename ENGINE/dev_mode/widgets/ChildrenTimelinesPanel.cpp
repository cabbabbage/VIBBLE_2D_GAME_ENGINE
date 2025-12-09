#include "ChildrenTimelinesPanel.hpp"

#include <algorithm>
#include <optional>
#include <utility>

#include <SDL_log.h>

#include <nlohmann/json.hpp>

#include "asset/animation_child_data.hpp"
#include "dev_mode/asset_sections/animation_editor_window/AnimationDocument.hpp"
#include "dev_mode/core/manifest_store.hpp"
#include "dev_mode/dm_styles.hpp"
#include "dev_mode/search_assets.hpp"
#include "dev_mode/widgets.hpp"
#include "utils/input.hpp"

namespace animation_editor {
namespace {
constexpr int kDefaultPanelWidth = 360;
constexpr int kDefaultPanelHeight = 260;

AnimationChildMode mode_from_index(int index) {
    return index == 1 ? AnimationChildMode::Async : AnimationChildMode::Static;
}

int index_from_mode(AnimationChildMode mode) {
    return mode == AnimationChildMode::Async ? 1 : 0;
}

const DMButtonStyle& enabled_button_style() {
    return DMStyles::AccentButton();
}

const DMButtonStyle& disabled_button_style() {
    return DMStyles::HeaderButton();
}

bool is_valid_selection(const std::string& selection) {
    return !selection.empty() && selection.front() != '#';
}

bool manifest_entry_has_animations(const nlohmann::json& entry) {
    if (!entry.is_object()) {
        return false;
    }
    auto it = entry.find("animations");
    return it != entry.end() && it->is_object() && !it->empty();
}
}

ChildrenTimelinesPanel::ChildrenTimelinesPanel()
    : DockableCollapsible("Children & Timelines", true /*floatable*/, kDefaultPanelWidth, kDefaultPanelHeight) {
    set_show_header(true);

    add_button_ = std::make_unique<DMButton>("Find Assets", &disabled_button_style(), 140, DMButton::height());
    add_widget_ = std::make_unique<ButtonWidget>(add_button_.get(), [this]() { this->open_asset_picker(); });

    remove_button_ = std::make_unique<DMButton>("Remove", &disabled_button_style(), 100, DMButton::height());
    remove_widget_ = std::make_unique<ButtonWidget>(remove_button_.get(), [this]() { this->remove_child(); });
    reset_button_ = std::make_unique<DMButton>("Reset Timeline", &disabled_button_style(), 140, DMButton::height());
    reset_widget_ = std::make_unique<ButtonWidget>(reset_button_.get(), [this]() { this->reset_selected_child_timeline(); });

    rebuild_rows();
}

void ChildrenTimelinesPanel::set_document(std::shared_ptr<AnimationDocument> document) {
    if (document_ == document) {
        return;
    }
    document_ = std::move(document);
    last_signature_.clear();
    sync_from_document();
}

void ChildrenTimelinesPanel::set_manifest_store(devmode::core::ManifestStore* manifest_store) {
    if (manifest_store_ == manifest_store) {
        return;
    }
    manifest_store_ = manifest_store;
    if (asset_picker_) {
        asset_picker_->set_manifest_store(manifest_store_);
    }
    last_signature_.clear();
    sync_from_document();
}

void ChildrenTimelinesPanel::set_status_callback(std::function<void(const std::string&, int)> callback) {
    status_callback_ = std::move(callback);
}

void ChildrenTimelinesPanel::set_on_children_changed(std::function<void(const std::vector<std::string>&)> callback) {
    on_children_changed_ = std::move(callback);
}

void ChildrenTimelinesPanel::refresh() {
    sync_from_document();
}

void ChildrenTimelinesPanel::update() {
    sync_from_document();
}

bool ChildrenTimelinesPanel::handle_event(const SDL_Event& e) {
    if (!is_visible()) {
        return false;
    }
    bool consumed = DockableCollapsible::handle_event(e);

    if (animation_dropdown_ && !animation_ids_.empty()) {
        int next_index = std::clamp(animation_dropdown_->selected(), 0, static_cast<int>(animation_ids_.size()) - 1);
        if (next_index != selected_animation_index_) {
            select_animation(next_index);
            consumed = true;
        }
    }

    if (child_dropdown_ && !child_names_.empty()) {
        int next_child = std::clamp(child_dropdown_->selected(), 0, static_cast<int>(child_names_.size()) - 1);
        if (next_child != selected_child_index_) {
            select_child(next_child);
            consumed = true;
        }
    }

    if (mode_dropdown_ && selected_child_index_ >= 0) {
        AnimationChildMode desired = mode_from_index(mode_dropdown_->selected());
        if (desired != current_mode_) {
            current_mode_ = desired;
            apply_selected_child_settings();
            consumed = true;
        }
    }

    return consumed;
}

void ChildrenTimelinesPanel::set_work_area_bounds(const SDL_Rect& bounds) {
    set_work_area(bounds);
}

void ChildrenTimelinesPanel::update_overlays(const Input& input) {
    if (asset_picker_ && asset_picker_->visible()) {
        asset_picker_->update(input);
    }
}

bool ChildrenTimelinesPanel::handle_overlay_event(const SDL_Event& e) {
    if (asset_picker_ && asset_picker_->visible()) {
        if (asset_picker_->handle_event(e)) {
            return true;
        }
    }
    return false;
}

void ChildrenTimelinesPanel::render_overlays(SDL_Renderer* renderer) const {
    if (asset_picker_ && asset_picker_->visible()) {
        asset_picker_->render(renderer);
    }
}

bool ChildrenTimelinesPanel::overlay_visible() const {
    return asset_picker_ && asset_picker_->visible();
}

bool ChildrenTimelinesPanel::overlay_contains_point(int x, int y) const {
    return asset_picker_ && asset_picker_->visible() && asset_picker_->is_point_inside(x, y);
}

void ChildrenTimelinesPanel::close_overlay() {
    if (asset_picker_) {
        asset_picker_->close();
    }
}

void ChildrenTimelinesPanel::rebuild_rows() {
    Rows rows;
    if (animation_widget_) {
        rows.push_back({animation_widget_.get()});
    }

    Row controls_row;
    controls_row.push_back(add_widget_.get());
    controls_row.push_back(remove_widget_.get());
    rows.push_back(std::move(controls_row));

    if (child_widget_) {
        rows.push_back({child_widget_.get()});
    }

    if (mode_widget_) {
        Row config_row;
        config_row.push_back(mode_widget_.get());
        if (reset_widget_) {
            config_row.push_back(reset_widget_.get());
        }
        rows.push_back(std::move(config_row));
    }

    set_rows(rows);
    set_expanded(true);
}

void ChildrenTimelinesPanel::sync_from_document() {
    const std::string signature = current_signature();
    if (signature == last_signature_) {
        sync_selection_controls();
        return;
    }
    last_signature_ = signature;

    animation_ids_.clear();
    child_names_.clear();
    selected_animation_index_ = -1;
    selected_child_index_ = -1;

    if (!document_) {
        animation_dropdown_.reset();
        animation_widget_.reset();
        child_dropdown_.reset();
        child_widget_.reset();
        mode_dropdown_.reset();
        mode_widget_.reset();
        reset_button_->set_style(&disabled_button_style());
        rebuild_rows();
        return;
    }

    animation_ids_ = document_->animation_ids();
    if (!animation_ids_.empty()) {
        if (selected_animation_index_ < 0 || selected_animation_index_ >= static_cast<int>(animation_ids_.size())) {
            selected_animation_index_ = 0;
        }
    }

    child_names_ = document_->animation_children();
    if (!child_names_.empty()) {
        if (selected_child_index_ < 0 || selected_child_index_ >= static_cast<int>(child_names_.size())) {
            selected_child_index_ = 0;
        }
    }

    sync_animation_dropdown();
    sync_child_dropdown();
    sync_selection_controls();
    rebuild_rows();
}

void ChildrenTimelinesPanel::sync_animation_dropdown() {
    std::vector<std::string> options = animation_ids_.empty()
        ? std::vector<std::string>{"(no animations)"}
        : animation_ids_;
    int selected = animation_ids_.empty() ? 0 : std::clamp(selected_animation_index_, 0, static_cast<int>(animation_ids_.size()) - 1);
    animation_dropdown_ = std::make_unique<DMDropdown>("Animation", options, selected);
    animation_widget_ = std::make_unique<DropdownWidget>(animation_dropdown_.get());
}

void ChildrenTimelinesPanel::sync_child_dropdown() {
    std::vector<std::string> options = child_names_.empty()
        ? std::vector<std::string>{"(no children)"}
        : child_names_;
    int selected = child_names_.empty() ? 0 : std::clamp(selected_child_index_, 0, static_cast<int>(child_names_.size()) - 1);
    child_dropdown_ = std::make_unique<DMDropdown>("Child", options, selected);
    child_widget_ = std::make_unique<DropdownWidget>(child_dropdown_.get());
}

void ChildrenTimelinesPanel::sync_selection_controls() {
    const bool has_child = selected_child_index_ >= 0 && selected_child_index_ < static_cast<int>(child_names_.size());
    const bool can_add = manifest_store_ != nullptr;
    if (add_button_) add_button_->set_style(can_add ? &enabled_button_style() : &disabled_button_style());
    if (remove_button_) remove_button_->set_style(has_child ? &enabled_button_style() : &disabled_button_style());
    if (reset_button_) reset_button_->set_style(has_child ? &enabled_button_style() : &disabled_button_style());

    if (!has_child || !document_) {
        mode_dropdown_.reset();
        mode_widget_.reset();
        current_mode_ = AnimationChildMode::Static;
        return;
    }

    std::string animation = selected_animation_id();
    auto child = selected_child_name();
    if (animation.empty() || !child) {
        return;
    }

    auto settings = document_->child_timeline_settings(animation, *child);
    if (!settings.found) {
        settings.mode = AnimationChildMode::Static;
    }
    current_mode_ = settings.mode;

    mode_dropdown_ = std::make_unique<DMDropdown>("Mode", std::vector<std::string>{"Static", "Async"}, index_from_mode(current_mode_));
    mode_widget_ = std::make_unique<DropdownWidget>(mode_dropdown_.get());
}

void ChildrenTimelinesPanel::select_animation(int index) {
    if (animation_ids_.empty()) {
        selected_animation_index_ = -1;
        return;
    }
    selected_animation_index_ = std::clamp(index, 0, static_cast<int>(animation_ids_.size()) - 1);
    last_signature_.clear();
    sync_from_document();
}

void ChildrenTimelinesPanel::select_child(int index) {
    if (child_names_.empty()) {
        selected_child_index_ = -1;
        return;
    }
    selected_child_index_ = std::clamp(index, 0, static_cast<int>(child_names_.size()) - 1);
    sync_selection_controls();
    rebuild_rows();
}

void ChildrenTimelinesPanel::ensure_asset_picker() {
    if (asset_picker_) {
        return;
    }
    asset_picker_ = std::make_unique<SearchAssets>(manifest_store_);
    asset_picker_->set_asset_filter(manifest_entry_has_animations);
    asset_picker_->set_floating_stack_key("children_timelines_panel");
}

void ChildrenTimelinesPanel::open_asset_picker() {
    if (!manifest_store_ || !document_) {
        if (status_callback_) status_callback_("Manifest store unavailable.", 180);
        return;
    }
    ensure_asset_picker();
    if (!asset_picker_) {
        return;
    }
    SDL_Rect self_rect = rect();
    int search_x = self_rect.x + self_rect.w + DMSpacing::panel_padding();
    int search_y = self_rect.y;
    asset_picker_->set_position(search_x, search_y);
    asset_picker_->open([this](const std::string& selection) {
        if (!is_valid_selection(selection)) {
            return;
        }
        this->add_child(selection);
        if (asset_picker_) asset_picker_->close();
    });
}

void ChildrenTimelinesPanel::add_child(const std::string& asset_name) {
    if (!document_) {
        return;
    }
    auto children = document_->animation_children();
    auto it = std::find(children.begin(), children.end(), asset_name);
    if (it != children.end()) {
        select_child(static_cast<int>(std::distance(children.begin(), it)));
        if (status_callback_) status_callback_("Child already exists.", 180);
        return;
    }
    children.push_back(asset_name);
    document_->replace_animation_children(children);
    try {
        document_->save_to_file();
    } catch (...) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[ChildrenTimelinesPanel] Failed to save animation document after adding child.");
    }
    if (on_children_changed_) {
        on_children_changed_(children);
    }
    last_signature_.clear();
    sync_from_document();
    select_child(static_cast<int>(children.size()) - 1);
    if (status_callback_) {
        status_callback_(std::string("Added child '") + asset_name + "'.", 180);
    }
}

void ChildrenTimelinesPanel::remove_child() {
    if (!document_) {
        return;
    }
    if (selected_child_index_ < 0 || selected_child_index_ >= static_cast<int>(child_names_.size())) {
        if (status_callback_) status_callback_("Select a child to remove.", 180);
        return;
    }
    std::vector<std::string> next;
    next.reserve(child_names_.size() > 0 ? child_names_.size() - 1 : 0);
    for (std::size_t i = 0; i < child_names_.size(); ++i) {
        if (static_cast<int>(i) == selected_child_index_) {
            continue;
        }
        next.push_back(child_names_[i]);
    }
    const std::string removed = child_names_[static_cast<std::size_t>(selected_child_index_)];
    document_->replace_animation_children(next);
    try {
        document_->save_to_file();
    } catch (...) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[ChildrenTimelinesPanel] Failed to save animation document after removing child.");
    }
    if (on_children_changed_) {
        on_children_changed_(next);
    }
    last_signature_.clear();
    sync_from_document();
    if (!next.empty()) {
        select_child(std::min(selected_child_index_, static_cast<int>(next.size()) - 1));
    }
    if (status_callback_) {
        status_callback_(std::string("Removed child '") + removed + "'.", 180);
    }
}

void ChildrenTimelinesPanel::reset_selected_child_timeline() {
    if (!document_) {
        if (status_callback_) status_callback_("No document loaded.", 180);
        return;
    }
    std::string animation = selected_animation_id();
    auto child = selected_child_name();
    if (animation.empty() || !child) {
        if (status_callback_) status_callback_("Select a child to reset.", 180);
        return;
    }
    const bool reset = document_->reset_child_timeline(animation, *child);
    if (reset) {
        try {
            document_->save_to_file();
        } catch (...) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[ChildrenTimelinesPanel] Failed to save animation document after resetting child timeline.");
        }
        last_signature_.clear();
        sync_from_document();
        if (status_callback_) status_callback_("Reset child timeline.", 180);
    } else {
        if (status_callback_) status_callback_("Reset failed.", 180);
    }
}

void ChildrenTimelinesPanel::apply_selected_child_settings() {
    if (!document_ || selected_child_index_ < 0) {
        return;
    }
    std::string animation = selected_animation_id();
    auto child = selected_child_name();
    if (animation.empty() || !child) {
        return;
    }
    // Auto-start is automatically true for Static mode, false for Async mode
    const bool auto_start = (current_mode_ == AnimationChildMode::Static);
    bool changed = document_->set_child_timeline_settings(animation, *child, current_mode_, auto_start, std::string{});
    if (!changed) {
        return;
    }
    try {
        document_->save_to_file();
    } catch (...) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[ChildrenTimelinesPanel] Failed to save animation document after child config change.");
    }
    last_signature_.clear();
    sync_from_document();
}

std::string ChildrenTimelinesPanel::current_signature() const {
    if (!document_) {
        return std::string{};
    }
    std::string signature = document_->animation_children_signature();
    auto animations = document_->animation_ids();
    for (const auto& id : animations) {
        signature.append("|").append(id);
    }
    std::string animation = selected_animation_id();
    if (!animation.empty()) {
        if (auto payload = document_->animation_payload(animation)) {
            signature.append("|payload:").append(*payload);
        }
    }
    return signature;
}

std::string ChildrenTimelinesPanel::selected_animation_id() const {
    if (selected_animation_index_ < 0 || selected_animation_index_ >= static_cast<int>(animation_ids_.size())) {
        return std::string{};
    }
    return animation_ids_[static_cast<std::size_t>(selected_animation_index_)];
}

std::optional<std::string> ChildrenTimelinesPanel::selected_child_name() const {
    if (selected_child_index_ < 0 || selected_child_index_ >= static_cast<int>(child_names_.size())) {
        return std::nullopt;
    }
    return child_names_[static_cast<std::size_t>(selected_child_index_)];
}

} // namespace animation_editor
