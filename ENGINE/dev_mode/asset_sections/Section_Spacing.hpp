#pragma once

#include "../DockableCollapsible.hpp"
#include "widgets.hpp"
#include <algorithm>
#include <memory>
#include <string>
#include <functional>

// Spacing configuration: distance sliders only
class Section_Spacing : public DockableCollapsible {
  public:
    Section_Spacing() : DockableCollapsible("Spacing", false) {}
    ~Section_Spacing() override = default;

    void build() override {
      widgets_.clear();
      DockableCollapsible::Rows rows;
      if (!info_) { set_rows(rows); return; }
      s_min_same_ = std::make_unique<DMSlider>(
          "Min Distance From Same Type",
          0, 2000,
          std::max(0, info_->min_same_type_distance));
      s_min_all_  = std::make_unique<DMSlider>(
          "Min Distance From All Assets",
          0, 2000,
          std::max(0, info_->min_distance_all));

      auto w_same = std::make_unique<SliderWidget>(s_min_same_.get());
      rows.push_back({ w_same.get() });
      widgets_.push_back(std::move(w_same));

      auto w_all = std::make_unique<SliderWidget>(s_min_all_.get());
      rows.push_back({ w_all.get() });
      widgets_.push_back(std::move(w_all));

      set_rows(rows);
    }

    void layout() override {
      DockableCollapsible::layout();
    }

    bool handle_event(const SDL_Event& e) override {
      bool used = DockableCollapsible::handle_event(e);
      if (!info_ || !expanded_) return used;
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
      if (changed) (void)info_->update_info_json();
      return used || changed;
    }

    void render_content(SDL_Renderer* /*r*/) const override {}

  private:
    std::unique_ptr<DMSlider> s_min_same_;
    std::unique_ptr<DMSlider> s_min_all_;
    std::vector<std::unique_ptr<Widget>> widgets_;
};
