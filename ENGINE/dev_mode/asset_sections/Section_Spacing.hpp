#pragma once

#include "CollapsibleSection.hpp"
#include <algorithm>
#include <memory>
#include <string>
#include <functional>

// Spacing configuration: distance sliders only
class Section_Spacing : public CollapsibleSection {
  public:
    Section_Spacing() : CollapsibleSection("Spacing") {}
    ~Section_Spacing() override = default;

    void build() override {
      if (!info_) return;
      s_min_same_ = std::make_unique<DMSlider>(
          "Min Distance From Same Type",
          0, 2000,
          std::max(0, info_->min_same_type_distance));
      s_min_all_  = std::make_unique<DMSlider>(
          "Min Distance From All Assets",
          0, 2000,
          std::max(0, info_->min_distance_all));
    }

    void layout() override {
      CollapsibleSection::layout();
      int x = rect_.x + DMSpacing::panel_padding();
      int y = rect_.y + DMButton::height() + DMSpacing::header_gap();
      int maxw = std::max(120, rect_.w - 2 * DMSpacing::panel_padding());
      int draw_y = y - scroll_;

      if (s_min_same_) {
        s_min_same_->set_rect(SDL_Rect{ x, draw_y, maxw, DMSlider::height() });
        y += DMSlider::height() + DMSpacing::item_gap();
        draw_y = y - scroll_;
      }
      if (s_min_all_) {
        s_min_all_->set_rect(SDL_Rect{ x, draw_y, maxw, DMSlider::height() });
        y += DMSlider::height() + DMSpacing::item_gap();
        draw_y = y - scroll_;
      }
      content_height_ = std::max(0, y - (rect_.y + DMButton::height() + DMSpacing::header_gap()));
    }

    bool handle_event(const SDL_Event& e) override {
      bool used = CollapsibleSection::handle_event(e);
      if (!info_ || !expanded_) return used;
      bool changed = false;

      if (s_min_same_ && s_min_same_->handle_event(e)) {
        int v = std::max(0, s_min_same_->value());
        info_->set_min_same_type_distance(v);
        changed = true;
      }
      if (s_min_all_ && s_min_all_->handle_event(e)) {
        int v = std::max(0, s_min_all_->value());
        info_->set_min_distance_all(v);
        changed = true;
      }
      if (changed) (void)info_->update_info_json();
      return used || changed;
    }

    void render_content(SDL_Renderer* r) const override {
      if (s_min_same_) s_min_same_->render(r);
      if (s_min_all_)  s_min_all_->render(r);
    }

  private:
    std::unique_ptr<DMSlider> s_min_same_;
    std::unique_ptr<DMSlider> s_min_all_;
};
