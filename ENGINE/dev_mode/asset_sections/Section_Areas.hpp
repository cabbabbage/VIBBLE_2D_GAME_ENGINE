#pragma once

#include "CollapsibleSection.hpp"
#include <functional>
#include <vector>
#include <algorithm>

// Areas list + create/open editor
class Section_Areas : public CollapsibleSection {
  public:
    Section_Areas()
    : CollapsibleSection("Areas") {}

    void set_open_editor_callback(std::function<void(const std::string&)> cb) { open_editor_ = std::move(cb); }

    void build() override {
      rebuild_buttons();
      b_create_ = std::make_unique<DMButton>("New Area", &DMStyles::CreateButton(), 220, DMButton::height());
    }

    void rebuild_buttons() {
      buttons_.clear();
      if (!info_) return;
      for (const auto& na : info_->areas) {
        auto b = std::make_unique<DMButton>(na.name, &DMStyles::ListButton(), 240, DMButton::height());
        buttons_.push_back(std::move(b));
      }
    }

    void layout() override {
      CollapsibleSection::layout();
      int x = rect_.x + 8;
      int y = rect_.y + DMButton::height() + 8;
      int inner_w = rect_.w - 16;
      int used = 0;
      for (auto& b : buttons_) {
        b->set_rect(SDL_Rect{ x, y + used, inner_w, DMButton::height() });
        used += DMButton::height() + 8;
      }
      if (b_create_) {
        b_create_->set_rect(SDL_Rect{ x, y + used, inner_w, DMButton::height() });
        used += DMButton::height() + 8;
      }
      content_height_ = std::max(0, used + 8);
    }

    bool handle_event(const SDL_Event& e) override {
      bool used = CollapsibleSection::handle_event(e);
      if (!info_ || !expanded_) return used;
      for (auto& b : buttons_) {
        if (b && b->handle_event(e)) {
          if (open_editor_) open_editor_(b->text());
          return true;
        }
      }
      if (b_create_ && b_create_->handle_event(e)) {
        std::string nm = "area" + std::to_string(info_->areas.size() + 1);
        if (open_editor_) open_editor_(nm);
        return true;
      }
      return used;
    }

    void render_content(SDL_Renderer* r) const override {
      for (auto& b : buttons_) b->render(r);
      if (b_create_) b_create_->render(r);
    }

  private:
    std::vector<std::unique_ptr<DMButton>> buttons_;
    std::unique_ptr<DMButton>  b_create_;
    std::function<void(const std::string&)> open_editor_;
};

