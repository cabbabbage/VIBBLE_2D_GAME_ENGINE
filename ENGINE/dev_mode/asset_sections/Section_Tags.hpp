#pragma once

#include "../DockableCollapsible.hpp"
#include "dev_mode/room_config/tag_editor_widget.hpp"
#include "dev_mode/tag_utils.hpp"
#include "widgets.hpp"
#include "dev_mode/asset_info_sections.hpp"
#include <memory>
#include <vector>

class AssetInfoUI;

class Section_Tags : public DockableCollapsible {
  public:
    Section_Tags() : DockableCollapsible("Tags", false) { set_visible_height(480); }
    ~Section_Tags() override = default;

    void set_ui(AssetInfoUI* ui) { ui_ = ui; }

    void build() override {
      widgets_.clear();
      DockableCollapsible::Rows rows;
      if (!info_) {
        auto placeholder = std::make_unique<ReadOnlyTextBoxWidget>( "", "No asset selected. Select an asset from the library or scene to view and edit its information.");
        rows.push_back({ placeholder.get() });
        widgets_.push_back(std::move(placeholder));
        set_rows(rows);
        return;
      }

      if (!tag_editor_) {
        tag_editor_ = std::make_unique<TagEditorWidget>();
        tag_editor_->set_on_changed([this](const std::vector<std::string>& tags,
                                           const std::vector<std::string>& anti_tags) {
          if (!info_) return;
          info_->set_tags(tags);
          info_->set_anti_tags(anti_tags);
          bool wrote = info_->commit_manifest();
          if (wrote) {
            tag_utils::notify_tags_changed();
            if (ui_) {
              ui_->sync_target_tags();
            }
          }
        });
      }

      tag_editor_->set_tags(info_->tags, info_->anti_tags);
      rows.push_back({ tag_editor_.get() });

      if (!apply_btn_) {
        apply_btn_ = std::make_unique<DMButton>("Apply Settings", &DMStyles::AccentButton(), 180, DMButton::height());
      }
      auto w_apply = std::make_unique<ButtonWidget>(apply_btn_.get(), [this]() {
        if (ui_) ui_->request_apply_section(AssetInfoSectionId::Tags);
      });
      rows.push_back({ w_apply.get() });
      widgets_.push_back(std::move(w_apply));

      set_rows(rows);
    }

    void layout() override { DockableCollapsible::layout(); }

    bool handle_event(const SDL_Event& e) override {
      return DockableCollapsible::handle_event(e);
    }

    void render_content(SDL_Renderer* ) const override {}

  private:
    std::unique_ptr<TagEditorWidget> tag_editor_;
    std::vector<std::unique_ptr<Widget>> widgets_;
    std::unique_ptr<DMButton> apply_btn_;
    AssetInfoUI* ui_ = nullptr;

  protected:
    std::string_view lock_settings_namespace() const override { return "asset_info"; }
    std::string_view lock_settings_id() const override { return "tags"; }
};

