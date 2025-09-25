#pragma once

#include "../DockableCollapsible.hpp"
#include "widgets.hpp"
#include "dev_mode/asset_info_sections.hpp"
#include <functional>
#include <vector>
#include <algorithm>

// Areas list + create/open editor
class AssetInfoUI;

class Section_Areas : public DockableCollapsible {
  public:
    Section_Areas()
    : DockableCollapsible("Areas", false) {}

    void set_open_editor_callback(std::function<void(const std::string&)> cb) { open_editor_ = std::move(cb); }
    void set_delete_callback(std::function<void(const std::string&)> cb) { on_delete_ = std::move(cb); }
    void set_ui(AssetInfoUI* ui) { ui_ = ui; }

    void build() override {
      rebuild_buttons();
      b_create_ = std::make_unique<DMButton>("New Area", &DMStyles::CreateButton(), 220, DMButton::height());
      b_confirm_create_ = std::make_unique<DMButton>("Create", &DMStyles::CreateButton(), 140, DMButton::height());
      if (!apply_btn_) {
        apply_btn_ = std::make_unique<DMButton>("Apply Settings", &DMStyles::AccentButton(), 180, DMButton::height());
      }
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

    void update(const Input& input, int screen_w, int screen_h) override {
      DockableCollapsible::update(input, screen_w, screen_h);
      if (pending_open_ && open_editor_) {
        pending_open_ = false;
        open_editor_(pending_name_);
        pending_name_.clear();
      }
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
            // Defer opening the editor to avoid re-entrancy while handling events
            pending_name_ = nm;
            pending_open_ = true;
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
          // Toggle prompt to enter a name
          create_prompt_open_ = true;
          if (!new_area_name_box_) new_area_name_box_ = std::make_unique<DMTextBox>("Area Name", "");
          if (new_area_name_box_) new_area_name_box_->set_value("");
          rebuild_rows();
        });
        rows.push_back({ bw.get() });
        widgets_.push_back(std::move(bw));
      }

      if (create_prompt_open_) {
        if (!new_area_name_box_) new_area_name_box_ = std::make_unique<DMTextBox>("Area Name", "");
        auto tw = std::make_unique<TextBoxWidget>(new_area_name_box_.get());
        widgets_.push_back(std::move(tw));
        Widget* text_widget = widgets_.back().get();

          auto cw = std::make_unique<ButtonWidget>(b_confirm_create_.get(), [this]() {
          if (!open_editor_) return;
          std::string name = new_area_name_box_ ? new_area_name_box_->value() : std::string{};
          if (name.empty()) {
            // Fallback default
            name = "area" + std::to_string(info_ ? info_->areas.size() + 1 : 1);
          }
          create_prompt_open_ = false;
          rebuild_rows();
          pending_name_ = name;
          pending_open_ = true;
        });
        widgets_.push_back(std::move(cw));
        Widget* create_widget = widgets_.back().get();
        rows.push_back({ text_widget, create_widget });
      }
      if (apply_btn_) {
        auto aw = std::make_unique<ButtonWidget>(apply_btn_.get(), [this]() {
          if (ui_) ui_->request_apply_section(AssetInfoSectionId::Areas);
        });
        rows.push_back({ aw.get() });
        widgets_.push_back(std::move(aw));
      }
      set_rows(rows);
    }

    std::vector<std::unique_ptr<DMButton>> buttons_;
    std::vector<std::unique_ptr<DMButton>> del_buttons_;
    std::unique_ptr<DMButton>  b_create_;
    std::unique_ptr<DMButton>  b_confirm_create_;
    std::unique_ptr<DMButton>  apply_btn_;
    std::function<void(const std::string&)> open_editor_;
    std::function<void(const std::string&)> on_delete_;
    std::unique_ptr<DMButton> dummy_del_{};
    std::vector<std::unique_ptr<Widget>> widgets_;
    AssetInfoUI* ui_ = nullptr; // non-owning
    bool create_prompt_open_ = false;
    std::unique_ptr<DMTextBox> new_area_name_box_;
    bool pending_open_ = false;
    std::string pending_name_;
};

