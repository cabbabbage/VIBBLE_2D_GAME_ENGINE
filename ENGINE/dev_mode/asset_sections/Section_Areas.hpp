#pragma once

#include "../DockableCollapsible.hpp"
#include <functional>
#include <vector>
#include <algorithm>

// Areas list + create/open editor
class Section_Areas : public DockableCollapsible {
  public:
    Section_Areas()
    : DockableCollapsible("Areas", false) {}

    void set_open_editor_callback(std::function<void(const std::string&)> cb) { open_editor_ = std::move(cb); }
    void set_delete_callback(std::function<void(const std::string&)> cb) { on_delete_ = std::move(cb); }

    void build() override {
      rebuild_buttons();
      b_create_ = std::make_unique<DMButton>("New Area", &DMStyles::CreateButton(), 220, DMButton::height());
    }

    void rebuild_buttons() {
      buttons_.clear();
      del_buttons_.clear();
      if (!info_) return;
      for (const auto& na : info_->areas) {
        auto b = std::make_unique<DMButton>(na.name, &DMStyles::ListButton(), 240, DMButton::height());
        auto d = std::make_unique<DMButton>("Delete", &DMStyles::DeleteButton(), 100, DMButton::height());
        buttons_.push_back(std::move(b));
        del_buttons_.push_back(std::move(d));
      }
    }

    void layout() override {
      // Refresh buttons if areas changed (count or names)
      if (info_) {
        bool needs = (buttons_.size() != info_->areas.size());
        if (!needs) {
          for (size_t i = 0; i < buttons_.size(); ++i) {
            if (!buttons_[i] || buttons_[i]->text() != info_->areas[i].name) { needs = true; break; }
          }
        }
        if (needs) rebuild_buttons();
      }
      int x = rect_.x + DMSpacing::panel_padding();
      int y = rect_.y + DMSpacing::panel_padding() + DMButton::height() + DMSpacing::header_gap();
      int inner_w = rect_.w - 2 * DMSpacing::panel_padding();
      int used = 0;
      for (size_t i = 0; i < buttons_.size(); ++i) {
        auto& b = buttons_[i];
        auto& d = (i < del_buttons_.size() ? del_buttons_[i] : dummy_del_);
        int del_w = 120;
        int spacing = DMSpacing::item_gap();
        int main_w = std::max(0, inner_w - del_w - spacing);
        if (b) b->set_rect(SDL_Rect{ x, y + used - scroll_, main_w, DMButton::height() });
        if (d) d->set_rect(SDL_Rect{ x + main_w + spacing, y + used - scroll_, del_w, DMButton::height() });
        used += DMButton::height() + DMSpacing::item_gap();
      }
      if (b_create_) {
        b_create_->set_rect(SDL_Rect{ x, y + used - scroll_, inner_w, DMButton::height() });
        used += DMButton::height() + DMSpacing::item_gap();
      }
      content_height_ = std::max(0, used + DMSpacing::item_gap());
      DockableCollapsible::layout();
    }

    bool handle_event(const SDL_Event& e) override {
      bool used = DockableCollapsible::handle_event(e);
      if (!info_ || !expanded_) return used;
      for (size_t i = 0; i < buttons_.size(); ++i) {
        auto& b = buttons_[i];
        auto& d = (i < del_buttons_.size() ? del_buttons_[i] : dummy_del_);
        if (d && d->handle_event(e)) {
          // Prefer direct index -> name mapping to avoid races with text()
          if (info_ && i < info_->areas.size() && on_delete_) on_delete_(info_->areas[i].name);
          return true;
        }
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
      for (size_t i = 0; i < buttons_.size(); ++i) {
        auto& b = buttons_[i];
        if (b) b->render(r);
        if (i < del_buttons_.size() && del_buttons_[i]) del_buttons_[i]->render(r);
      }
      if (b_create_) b_create_->render(r);
    }

  private:
    std::vector<std::unique_ptr<DMButton>> buttons_;
    std::vector<std::unique_ptr<DMButton>> del_buttons_;
    std::unique_ptr<DMButton>  b_create_;
    std::function<void(const std::string&)> open_editor_;
    std::function<void(const std::string&)> on_delete_;
    // placeholder to avoid branching
    std::unique_ptr<DMButton> dummy_del_{};
};

