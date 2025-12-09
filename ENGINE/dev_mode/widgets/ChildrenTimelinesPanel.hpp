#pragma once

#include <SDL.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "asset/animation_child_data.hpp"
#include "dev_mode/DockableCollapsible.hpp"

class ButtonWidget;
class DropdownWidget;
class DMButton;
class DMDropdown;
class Input;
class SearchAssets;

namespace devmode::core {
class ManifestStore;
}

namespace animation_editor {

class AnimationDocument;

struct ChildConfig {
    std::string asset_name;
    AnimationChildMode mode = AnimationChildMode::Static;
};

class ChildrenTimelinesPanel : public DockableCollapsible {
  public:
    ChildrenTimelinesPanel();

    void set_document(std::shared_ptr<AnimationDocument> document);
    void set_manifest_store(devmode::core::ManifestStore* manifest_store);
    void set_status_callback(std::function<void(const std::string&, int)> callback);
    void set_on_children_changed(std::function<void(const std::vector<std::string>&)> callback);

    void refresh();
    void update();

    bool handle_event(const SDL_Event& e) override;
    void render(SDL_Renderer* renderer) const override { DockableCollapsible::render(renderer); }

    void set_work_area_bounds(const SDL_Rect& bounds);

    void update_overlays(const Input& input);
    bool handle_overlay_event(const SDL_Event& e);
    void render_overlays(SDL_Renderer* renderer) const;
    bool overlay_visible() const;
    bool overlay_contains_point(int x, int y) const;
    void close_overlay();

  private:
    void rebuild_rows();
    void sync_from_document();
    void sync_animation_dropdown();
    void sync_child_dropdown();
    void sync_selection_controls();
    void select_animation(int index);
    void select_child(int index);
    void reset_selected_child_timeline();
    void open_asset_picker();
    void add_child(const std::string& asset_name);
    void remove_child();
    void apply_selected_child_settings();
    void ensure_asset_picker();
    std::string current_signature() const;
    std::string selected_animation_id() const;
    std::optional<std::string> selected_child_name() const;

  private:
    std::shared_ptr<AnimationDocument> document_;
    devmode::core::ManifestStore* manifest_store_ = nullptr;
    std::unique_ptr<SearchAssets> asset_picker_;
    std::function<void(const std::string&, int)> status_callback_;
    std::function<void(const std::vector<std::string>&)> on_children_changed_;

    std::vector<std::string> animation_ids_;
    int selected_animation_index_ = -1;
    std::vector<std::string> child_names_;
    int selected_child_index_ = -1;

    AnimationChildMode current_mode_ = AnimationChildMode::Static;

    std::unique_ptr<DMDropdown> animation_dropdown_;
    std::unique_ptr<DropdownWidget> animation_widget_;
    std::unique_ptr<DMDropdown> child_dropdown_;
    std::unique_ptr<DropdownWidget> child_widget_;
    std::unique_ptr<DMButton> add_button_;
    std::unique_ptr<ButtonWidget> add_widget_;
    std::unique_ptr<DMButton> remove_button_;
    std::unique_ptr<ButtonWidget> remove_widget_;
    std::unique_ptr<DMButton> reset_button_;
    std::unique_ptr<ButtonWidget> reset_widget_;
    std::unique_ptr<DMDropdown> mode_dropdown_;
    std::unique_ptr<DropdownWidget> mode_widget_;

    std::string last_signature_;
};

} // namespace animation_editor
