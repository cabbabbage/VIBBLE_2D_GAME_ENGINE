#pragma once

#include <SDL.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "asset/animation_child_data.hpp"
#include "dev_mode/DockableCollapsible.hpp"

class ButtonWidget;
class DMButton;
class DMCheckbox;
class Input;
class SearchAssets;

namespace devmode::core {
class ManifestStore;
}

namespace animation_editor {

class AnimationDocument;

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
    void sync_child_rows();
    void open_asset_picker();
    void add_child(const std::string& asset_name);
    void remove_child(const std::string& child_name);
    void apply_child_mode(const std::string& child_name, AnimationChildMode mode);
    void ensure_asset_picker();
    std::string current_signature() const;
    AnimationChildMode child_mode(const std::string& animation_id, const std::string& child_name) const;
    bool apply_mode_to_all_animations(const std::string& child_name, AnimationChildMode mode);

  private:
    std::shared_ptr<AnimationDocument> document_;
    devmode::core::ManifestStore* manifest_store_ = nullptr;
    std::unique_ptr<SearchAssets> asset_picker_;
    std::function<void(const std::string&, int)> status_callback_;
    std::function<void(const std::vector<std::string>&)> on_children_changed_;

    struct ChildRow {
        std::string name;
        std::unique_ptr<Widget> label_widget;
        std::unique_ptr<DMCheckbox> async_checkbox;
        std::unique_ptr<Widget> async_widget;
        std::unique_ptr<DMButton> delete_button;
        std::unique_ptr<Widget> delete_widget;
};

    std::vector<ChildRow> child_rows_;
    std::unique_ptr<DMButton> add_button_;
    std::unique_ptr<ButtonWidget> add_widget_;

    std::string last_signature_;
};

}
