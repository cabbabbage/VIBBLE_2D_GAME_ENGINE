#pragma once

#include "CollapsibleSection.hpp"
#include <algorithm>
#include <cmath>
#include <memory>
#include <sstream>
#include <vector>

class Section_BasicInfo : public CollapsibleSection {
  public:
    Section_BasicInfo() : CollapsibleSection("Basic Info") {}
    ~Section_BasicInfo() override = default;

    void build() override {
      if (!info_) return;
      // Build controls
      t_type_  = std::make_unique<DMTextBox>("Type", info_->type);
      int pct = std::max(0, (int)std::lround(info_->scale_factor * 100.0f));
      s_scale_pct_ = std::make_unique<DMSlider>("Scale (%)", 10, 400, pct);
      s_zindex_    = std::make_unique<DMSlider>("Z Index Offset", -1000, 1000, info_->z_threshold);
      c_flipable_  = std::make_unique<DMCheckbox>("Flipable (can invert)", info_->flipable);
    }

    void layout() override {
      CollapsibleSection::layout();
      int x = rect_.x + 16;
      int y = rect_.y + DMButton::height() + 8;
      int maxw = std::max(120, rect_.w - 32);
      int draw_y = y - scroll_;
      if (t_type_) {
        int w = std::min(440, maxw);
        int h = t_type_->preferred_height(w);
        t_type_->set_rect(SDL_Rect{ x, draw_y, w, h });
        y += h + 12;
        draw_y = y - scroll_;
      }
      if (s_scale_pct_) {
        s_scale_pct_->set_rect(SDL_Rect{ x, draw_y, maxw, DMSlider::height() });
        y += DMSlider::height() + 8;
        draw_y = y - scroll_;
      }
      if (s_zindex_) {
        s_zindex_->set_rect(SDL_Rect{ x, draw_y, maxw, DMSlider::height() });
        y += DMSlider::height() + 8;
        draw_y = y - scroll_;
      }
      if (c_flipable_) {
        c_flipable_->set_rect(SDL_Rect{ x, draw_y, maxw, DMCheckbox::height() });
        y += DMCheckbox::height() + 8;
        draw_y = y - scroll_;
      }
      content_height_ = std::max(0, y - (rect_.y + DMButton::height() + 8));
    }

    bool handle_event(const SDL_Event& e) override {
      bool used = CollapsibleSection::handle_event(e);
      if (!info_) return used;
      bool changed = false;
      if (t_type_  && t_type_->handle_event(e))  { info_->set_asset_type(t_type_->value()); changed = true; }
      if (s_scale_pct_ && s_scale_pct_->handle_event(e)) { info_->set_scale_percentage((float)s_scale_pct_->value()); changed = true; }
      if (s_zindex_    && s_zindex_->handle_event(e))    { info_->set_z_threshold(s_zindex_->value()); changed = true; }
      if (c_flipable_  && c_flipable_->handle_event(e))  { info_->set_flipable(c_flipable_->value()); changed = true; }
      if (changed) (void)info_->update_info_json();
      return used || changed;
    }

    void render_content(SDL_Renderer* r) const override {
      if (t_type_)      t_type_->render(r);
      if (s_scale_pct_) s_scale_pct_->render(r);
      if (s_zindex_)    s_zindex_->render(r);
      if (c_flipable_)  c_flipable_->render(r);
    }

  private:
    std::unique_ptr<DMTextBox>  t_type_;
    std::unique_ptr<DMSlider>   s_scale_pct_;
    std::unique_ptr<DMSlider>   s_zindex_;
    std::unique_ptr<DMCheckbox> c_flipable_;
};

