#pragma once

#include "../DockableCollapsible.hpp"
#include "asset/asset_info.hpp"
#include "widgets.hpp"
#include <algorithm>
#include <cmath>
#include <memory>
#include <sstream>
#include <vector>

class Section_BasicInfo : public DockableCollapsible {
  public:
    Section_BasicInfo() : DockableCollapsible("Basic Info", false) {}
    ~Section_BasicInfo() override = default;

    void build() override {
      widgets_.clear();
      DockableCollapsible::Rows rows;
      if (!info_) { set_rows(rows); return; }

      t_type_  = std::make_unique<DMTextBox>("Type", info_->type);
      int pct = std::max(0, (int)std::lround(info_->scale_factor * 100.0f));
      s_scale_pct_ = std::make_unique<DMSlider>("Scale (%)", 10, 400, pct);
      s_zindex_    = std::make_unique<DMSlider>("Z Index Offset", -1000, 1000, info_->z_threshold);
      c_flipable_  = std::make_unique<DMCheckbox>("Flipable (can invert)", info_->flipable);

      auto w_type = std::make_unique<TextBoxWidget>(t_type_.get());
      rows.push_back({ w_type.get() });
      widgets_.push_back(std::move(w_type));

      auto w_scale = std::make_unique<SliderWidget>(s_scale_pct_.get());
      rows.push_back({ w_scale.get() });
      widgets_.push_back(std::move(w_scale));

      auto w_z = std::make_unique<SliderWidget>(s_zindex_.get());
      rows.push_back({ w_z.get() });
      widgets_.push_back(std::move(w_z));

      auto w_flip = std::make_unique<CheckboxWidget>(c_flipable_.get());
      rows.push_back({ w_flip.get() });
      widgets_.push_back(std::move(w_flip));

      set_rows(rows);
    }

    void layout() override {
      DockableCollapsible::layout();
    }

    bool handle_event(const SDL_Event& e) override {
      bool used = DockableCollapsible::handle_event(e);
      if (!info_) return used;
      bool changed = false;
      if (t_type_ && info_->type != t_type_->value()) {
        info_->set_asset_type(t_type_->value());
        changed = true;
      }
      int pct = std::max(0, (int)std::lround(info_->scale_factor * 100.0f));
      if (s_scale_pct_ && pct != s_scale_pct_->value()) {
        info_->set_scale_percentage((float)s_scale_pct_->value());
        changed = true;
      }
      if (s_zindex_ && info_->z_threshold != s_zindex_->value()) {
        info_->set_z_threshold(s_zindex_->value());
        changed = true;
      }
      if (c_flipable_ && info_->flipable != c_flipable_->value()) {
        info_->set_flipable(c_flipable_->value());
        changed = true;
      }
      if (changed) (void)info_->update_info_json();
      return used || changed;
    }

    void render_content(SDL_Renderer* /*r*/) const override {}

  private:
    std::unique_ptr<DMTextBox>  t_type_;
    std::unique_ptr<DMSlider>   s_scale_pct_;
    std::unique_ptr<DMSlider>   s_zindex_;
    std::unique_ptr<DMCheckbox> c_flipable_;
    std::vector<std::unique_ptr<Widget>> widgets_;
};

