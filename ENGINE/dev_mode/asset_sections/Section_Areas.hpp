#pragma once

#include "../DockableCollapsible.hpp"
#include "widgets.hpp"
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
      rebuild_rows();
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
      if (info_) {
        bool needs = (buttons_.size() != info_->areas.size());
        if (!needs) {
          for (size_t i = 0; i < buttons_.size(); ++i) {
            if (!buttons_[i] || buttons_[i]->text() != info_->areas[i].name) { needs = true; break; }
          }
        }
        if (needs) { rebuild_buttons(); rebuild_rows(); }
      }
      DockableCollapsible::layout();
    }

    bool handle_event(const SDL_Event& e) override {
      return DockableCollapsible::handle_event(e);
    }

    void render_content(SDL_Renderer* /*r*/) const override {}

  private:
    void rebuild_rows() {
      widgets_.clear();
      DockableCollapsible::Rows rows;
      for (size_t i = 0; i < buttons_.size(); ++i) {
        auto& b = buttons_[i];
        auto& d = (i < del_buttons_.size() ? del_buttons_[i] : dummy_del_);
        if (b) {
          DMButton* bp = b.get();
          std::string nm = bp->text();
          auto bw = std::make_unique<ButtonWidget>(bp, [this, nm]() {
            if (open_editor_) open_editor_(nm);
          });
          rows.push_back({ bw.get() });
          widgets_.push_back(std::move(bw));
        }
        if (d) {
          DMButton* dp = d.get();
          std::string dn = (info_ && i < info_->areas.size()) ? info_->areas[i].name : std::string{};
          auto dw = std::make_unique<ButtonWidget>(dp, [this, dn]() {
            if (on_delete_) on_delete_(dn);
            rebuild_buttons();
            rebuild_rows();
          });
          rows.push_back({ dw.get() });
          widgets_.push_back(std::move(dw));
        }
      }
      if (b_create_) {
        auto bw = std::make_unique<ButtonWidget>(b_create_.get(), [this]() {
          std::string nm = "area" + std::to_string(info_ ? info_->areas.size() + 1 : 1);
          if (open_editor_) open_editor_(nm);
        });
        rows.push_back({ bw.get() });
        widgets_.push_back(std::move(bw));
      }
      set_rows(rows);
    }

    std::vector<std::unique_ptr<DMButton>> buttons_;
    std::vector<std::unique_ptr<DMButton>> del_buttons_;
    std::unique_ptr<DMButton>  b_create_;
    std::function<void(const std::string&)> open_editor_;
    std::function<void(const std::string&)> on_delete_;
    std::unique_ptr<DMButton> dummy_del_{};
    std::vector<std::unique_ptr<Widget>> widgets_;
};

