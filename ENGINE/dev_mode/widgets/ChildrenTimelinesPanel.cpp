#include "ChildrenTimelinesPanel.hpp"

#include <algorithm>
#include <utility>

#include <SDL_log.h>
#include <SDL_ttf.h>

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

const DMButtonStyle& enabled_button_style() {
    return DMStyles::AccentButton();
}

const DMButtonStyle& disabled_button_style() {
    return DMStyles::HeaderButton();
}

const DMButtonStyle& delete_button_style() {
    return DMStyles::DeleteButton();
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

class ChildLabelWidget : public Widget {
  public:
    explicit ChildLabelWidget(std::string text) : text_(std::move(text)) {}

    void set_rect(const SDL_Rect& r) override { rect_ = r; }
    const SDL_Rect& rect() const override { return rect_; }
    int height_for_width(int) const override { return DMCheckbox::height(); }
    bool handle_event(const SDL_Event&) override { return false; }

    void render(SDL_Renderer* renderer) const override {
        if (!renderer) return;
        const auto& style = DMStyles::Label();
        TTF_Font* font = TTF_OpenFont(style.font_path.c_str(), style.font_size);
        if (!font) return;
        SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text_.c_str(), style.color);
        if (!surface) {
            TTF_CloseFont(font);
            return;
        }
        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
        if (texture) {
            SDL_Rect dst{rect_.x, rect_.y + (rect_.h - surface->h) / 2, surface->w, surface->h};
            SDL_RenderCopy(renderer, texture, nullptr, &dst);
            SDL_DestroyTexture(texture);
        }
        SDL_FreeSurface(surface);
        TTF_CloseFont(font);
    }

  private:
    std::string text_{};
    SDL_Rect rect_{0, 0, 0, DMCheckbox::height()};
};
}

ChildrenTimelinesPanel::ChildrenTimelinesPanel()
    : DockableCollapsible("Children & Timelines", true , kDefaultPanelWidth, kDefaultPanelHeight) {
    set_show_header(true);

    add_button_ = std::make_unique<DMButton>("Find Assets", &disabled_button_style(), 140, DMButton::height());
    add_widget_ = std::make_unique<ButtonWidget>(add_button_.get(), [this]() { this->open_asset_picker(); });

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
    std::vector<bool> previous_async;
    previous_async.reserve(child_rows_.size());
    for (const auto& row : child_rows_) {
        previous_async.push_back(row.async_checkbox ? row.async_checkbox->value() : false);
    }

    bool consumed = DockableCollapsible::handle_event(e);

    for (std::size_t i = 0; i < child_rows_.size(); ++i) {
        const auto& row = child_rows_[i];
        const bool next = row.async_checkbox ? row.async_checkbox->value() : false;
        if (i < previous_async.size() && next != previous_async[i]) {
            apply_child_mode(row.name, next ? AnimationChildMode::Async : AnimationChildMode::Static);
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
    Row controls_row;
    controls_row.push_back(add_widget_.get());
    rows.push_back(std::move(controls_row));

    for (auto& row : child_rows_) {
        Row child_row;
        if (row.label_widget) child_row.push_back(row.label_widget.get());
        if (row.async_widget) child_row.push_back(row.async_widget.get());
        if (row.delete_widget) child_row.push_back(row.delete_widget.get());
        rows.push_back(std::move(child_row));
    }

    set_rows(rows);
    set_expanded(true);
}

void ChildrenTimelinesPanel::sync_from_document() {
    const std::string signature = current_signature();
    if (signature == last_signature_) {
        sync_child_rows();
        return;
    }
    last_signature_ = signature;
    child_rows_.clear();

    const bool can_add = (manifest_store_ != nullptr) && (document_ != nullptr);
    if (add_button_) add_button_->set_style(can_add ? &enabled_button_style() : &disabled_button_style());

    if (!document_) {
        rebuild_rows();
        return;
    }

    const auto animation_ids = document_->animation_ids();
    const std::string animation_id = animation_ids.empty() ? std::string{} : animation_ids.front();
    const auto children = document_->animation_children();

    for (const auto& child : children) {
        ChildRow row;
        row.name = child;
        row.label_widget = std::make_unique<ChildLabelWidget>(child);
        const AnimationChildMode mode = animation_id.empty() ? AnimationChildMode::Static : child_mode(animation_id, child);
        row.async_checkbox = std::make_unique<DMCheckbox>("Async", mode == AnimationChildMode::Async);
        row.async_widget = std::make_unique<CheckboxWidget>(row.async_checkbox.get());
        row.delete_button = std::make_unique<DMButton>("x", &delete_button_style(), 36, DMButton::height());
        row.delete_widget = std::make_unique<ButtonWidget>(row.delete_button.get(), [this, child]() { this->remove_child(child); });
        child_rows_.push_back(std::move(row));
    }

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

void ChildrenTimelinesPanel::sync_child_rows() {
    const bool can_add = (manifest_store_ != nullptr) && (document_ != nullptr);
    if (add_button_) add_button_->set_style(can_add ? &enabled_button_style() : &disabled_button_style());

    if (!document_) {
        return;
    }

    const auto animation_ids = document_->animation_ids();
    if (animation_ids.empty()) {
        return;
    }

    const std::string animation_id = animation_ids.front();
    for (auto& row : child_rows_) {
        const AnimationChildMode mode = child_mode(animation_id, row.name);
        if (row.async_checkbox) {
            row.async_checkbox->set_value(mode == AnimationChildMode::Async);
        }
    }
}

void ChildrenTimelinesPanel::add_child(const std::string& asset_name) {
    if (!document_) {
        return;
    }
    auto children = document_->animation_children();
    auto it = std::find(children.begin(), children.end(), asset_name);
    if (it != children.end()) {
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
    if (status_callback_) {
        status_callback_(std::string("Added child '") + asset_name + "'.", 180);
    }
}

void ChildrenTimelinesPanel::remove_child(const std::string& child_name) {
    if (!document_) {
        return;
    }

    auto children = document_->animation_children();
    auto it = std::find(children.begin(), children.end(), child_name);
    if (it == children.end()) {
        if (status_callback_) status_callback_("Child not found.", 180);
        return;
    }

    children.erase(it);
    document_->replace_animation_children(children);
    try {
        document_->save_to_file();
    } catch (...) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[ChildrenTimelinesPanel] Failed to save animation document after removing child.");
    }
    if (on_children_changed_) {
        on_children_changed_(children);
    }
    last_signature_.clear();
    sync_from_document();
    if (status_callback_) {
        status_callback_(std::string("Removed child '") + child_name + "'.", 180);
    }
}

void ChildrenTimelinesPanel::apply_child_mode(const std::string& child_name, AnimationChildMode mode) {
    if (!document_) {
        return;
    }

    const bool changed = apply_mode_to_all_animations(child_name, mode);
    if (!changed) {
        return;
    }

    try {
        document_->save_to_file();
    } catch (...) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[ChildrenTimelinesPanel] Failed to save animation document after child mode change.");
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
        if (auto payload = document_->animation_payload(id)) {
            signature.append(":").append(*payload);
        }
    }
    return signature;
}

AnimationChildMode ChildrenTimelinesPanel::child_mode(const std::string& animation_id, const std::string& child_name) const {
    auto settings = document_ ? document_->child_timeline_settings(animation_id, child_name) : AnimationDocument::ChildTimelineSettings{};
    if (!settings.found) {
        return AnimationChildMode::Static;
    }
    return settings.mode;
}

bool ChildrenTimelinesPanel::apply_mode_to_all_animations(const std::string& child_name, AnimationChildMode mode) {
    if (!document_) {
        return false;
    }
    const bool auto_start = (mode == AnimationChildMode::Static);
    return document_->set_child_mode_for_all_animations(child_name, mode, auto_start);
}

}
