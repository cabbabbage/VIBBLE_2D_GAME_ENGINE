#pragma once

#include <memory>
#include <vector>

#include "../DockableCollapsible.hpp"
#include "dev_mode/core/manifest_store.hpp"

class AssetInfoUI;
class AssetInfo;

namespace animation_editor {
class AnimationDocument;
class AnimationChildrenPanel;
}

class Section_AnimationChildren : public DockableCollapsible {
  public:
    Section_AnimationChildren();
    ~Section_AnimationChildren() override;

    void set_ui(AssetInfoUI* ui) { ui_ = ui; }
    void set_document(std::shared_ptr<animation_editor::AnimationDocument> doc) { document_ = std::move(doc); }
    void set_manifest_store(devmode::core::ManifestStore* store);

    void build() override;
    void update(const Input& input, int screen_w, int screen_h) override;
    bool handle_event(const SDL_Event& e) override;

  protected:
    std::string_view lock_settings_namespace() const override { return "asset_info"; }
    std::string_view lock_settings_id() const override { return "animation_children"; }

  private:
    void rebuild_rows();

    std::unique_ptr<animation_editor::AnimationChildrenPanel> children_panel_;
    Widget* children_widget_ = nullptr;
    std::vector<std::unique_ptr<Widget>> widgets_;
    devmode::core::ManifestStore* manifest_store_ = nullptr;
    AssetInfoUI* ui_ = nullptr;
    std::shared_ptr<animation_editor::AnimationDocument> document_{};
};
