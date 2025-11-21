#pragma once

#include <memory>
#include <vector>

#include "../DockableCollapsible.hpp"
#include "animation_editor_window/ChildrenPanel.hpp"
#include "dev_mode/core/manifest_store.hpp"

class AssetInfoUI;
class AssetInfo;

class Section_AnimationChildren : public DockableCollapsible {
  public:
    Section_AnimationChildren();
    ~Section_AnimationChildren() override = default;

    void set_ui(AssetInfoUI* ui) { ui_ = ui; }
    void set_manifest_store(devmode::core::ManifestStore* store) {
        manifest_store_ = store;
        if (children_panel_) {
            children_panel_->set_manifest_store(manifest_store_);
        }
    }

    void build() override;
    void update(const Input& input, int screen_w, int screen_h) override;
    bool handle_event(const SDL_Event& e) override;

  protected:
    std::string_view lock_settings_namespace() const override { return "asset_info"; }
    std::string_view lock_settings_id() const override { return "animation_children"; }

  private:
    void rebuild_rows();

    std::unique_ptr<animation_editor::ChildrenPanel> children_panel_;
    Widget* children_widget_ = nullptr;
    std::vector<std::unique_ptr<Widget>> widgets_;
    devmode::core::ManifestStore* manifest_store_ = nullptr;
    AssetInfoUI* ui_ = nullptr;
};
