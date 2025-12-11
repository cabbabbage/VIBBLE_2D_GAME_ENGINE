#pragma once

#include "../DockableCollapsible.hpp"
#include "widgets.hpp"
#include "dev_mode/asset_info_sections.hpp"
#include <algorithm>
#include <memory>
#include <string>
#include <functional>

class AssetInfoUI;

class Section_Spacing : public DockableCollapsible {
  public:
    Section_Spacing() : DockableCollapsible("Spacing", false) {}
    ~Section_Spacing() override = default;

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
      s_min_same_ = std::make_unique<DMSlider>( "Min Distance From Same Type", 0, 2000, std::max(0, info_->min_same_type_distance));
      s_min_all_  = std::make_unique<DMSlider>( "Min Distance From All Assets", 0, 2000, std::max(0, info_->min_distance_all));
      int neighbor_distance = info_->NeighborSearchRadius > 0 ? info_->NeighborSearchRadius : 500;
      neighbor_distance = std::clamp(neighbor_distance, 20, 1000);
      s_neighbor_search_ = std::make_unique<DMSlider>("Neighbor Search Distance", 20, 1000, neighbor_distance);

      auto w_same = std::make_unique<SliderWidget>(s_min_same_.get());
      rows.push_back({ w_same.get() });
      widgets_.push_back(std::move(w_same));

      auto w_all = std::make_unique<SliderWidget>(s_min_all_.get());
      rows.push_back({ w_all.get() });
      widgets_.push_back(std::move(w_all));

      auto w_neighbor = std::make_unique<SliderWidget>(s_neighbor_search_.get());
      rows.push_back({ w_neighbor.get() });
      widgets_.push_back(std::move(w_neighbor));

      if (!apply_btn_) {
        apply_btn_ = std::make_unique<DMButton>("Apply Settings", &DMStyles::AccentButton(), 180, DMButton::height());
      }
      auto w_apply = std::make_unique<ButtonWidget>(apply_btn_.get(), [this]() {
        if (ui_) ui_->request_apply_section(AssetInfoSectionId::Spacing);
      });
      rows.push_back({ w_apply.get() });
      widgets_.push_back(std::move(w_apply));

      set_rows(rows);
    }

    void layout() override {
      DockableCollapsible::layout();
    }

    bool handle_event(const SDL_Event& e) override {
      bool used = DockableCollapsible::handle_event(e);
      if (!info_ || !expanded_) return used;

      if (!used) {
        if (s_min_same_ && s_min_same_->handle_event(e)) used = true;
        if (s_min_all_ && s_min_all_->handle_event(e)) used = true;
        if (s_neighbor_search_ && s_neighbor_search_->handle_event(e)) used = true;
      }

      bool changed = false;

      if (s_min_same_ && info_->min_same_type_distance != s_min_same_->value()) {
        int v = std::max(0, s_min_same_->value());
        info_->set_min_same_type_distance(v);
        changed = true;
      }
      if (s_min_all_ && info_->min_distance_all != s_min_all_->value()) {
        int v = std::max(0, s_min_all_->value());
        info_->set_min_distance_all(v);
        changed = true;
      }
      if (s_neighbor_search_ && info_->NeighborSearchRadius != s_neighbor_search_->value()) {
        int v = std::clamp(s_neighbor_search_->value(), 20, 1000);
        info_->set_neighbor_search_radius(v);
        changed = true;
      }
      if (changed) {
        (void)info_->commit_manifest();
        if (ui_) {
          ui_->sync_target_spacing_settings();
        }
      }
      return used || changed;
    }

    void render_content(SDL_Renderer* ) const override {}

  private:
    std::unique_ptr<DMSlider> s_min_same_;
    std::unique_ptr<DMSlider> s_min_all_;
    std::unique_ptr<DMSlider> s_neighbor_search_;
    std::vector<std::unique_ptr<Widget>> widgets_;
    std::unique_ptr<DMButton> apply_btn_;
    AssetInfoUI* ui_ = nullptr;

  protected:
    std::string_view lock_settings_namespace() const override { return "asset_info"; }
    std::string_view lock_settings_id() const override { return "spacing"; }
};
