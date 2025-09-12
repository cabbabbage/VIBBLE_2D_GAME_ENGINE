#pragma once

#include "ui/asset_sections/CollapsibleSection.hpp"
#include <functional>
#include <vector>
#include "ui/text_box.hpp"

// Areas list + create/open editor
class Section_Areas : public CollapsibleSection {
  public:
    Section_Areas()
    : CollapsibleSection("Areas") {}

    void set_open_editor_callback(std::function<void(const std::string&)> cb) { open_editor_ = std::move(cb); }

    void build() override {
      rebuild_buttons();
      b_create_ = std::make_unique<Button>("Create New Area", &DevStyles::PrimaryButton(), 220, Button::height());
      t_new_name_ = std::make_unique<TextBox>("Area Name", "");
      t_new_name_->set_editing(false);
      prompt_new_ = false;
    }

    void rebuild_buttons() {
      buttons_.clear();
      if (!info_) return;
      for (const auto& na : info_->areas) {
        auto b = std::make_unique<Button>(na.name, &DevStyles::SecondaryButton(), 240, Button::height());
        buttons_.push_back(std::move(b));
      }
    }

    void layout() override {
      CollapsibleSection::layout();
      int x = rect_.x + 16;
      int y = rect_.y + Button::height() + 8;
      int width = std::max(180, rect_.w - 32);
      int used = 0;
      int bx = x + 16;
      for (auto& b : buttons_) {
        b->set_rect(SDL_Rect{ bx, y + used, 240, Button::height() });
        used += Button::height() + 6;
      }
      if (b_create_) {
        b_create_->set_rect(SDL_Rect{ bx, y + used, 220, Button::height() });
        used += Button::height() + 6;
      }
      if (prompt_new_ && t_new_name_) {
        t_new_name_->set_rect(SDL_Rect{ bx, y + used, 260, TextBox::height() });
        used += TextBox::height() + 6;
      }
      (void)width;
      content_height_ = std::max(0, used);
      if (header_) header_->set_text(expanded_ ? title_ + " ▾" : title_ + " ▸");
    }

    bool handle_event(const SDL_Event& e) override {
      bool used = CollapsibleSection::handle_event(e);
      if (!info_) return used;
      if (!expanded_) return used;
      for (auto& b : buttons_) {
        if (b && b->handle_event(e)) {
          selected_ = b->text();
          if (open_editor_) open_editor_(selected_);
          return true;
        }
      }
      if (b_create_ && b_create_->handle_event(e)) {
        prompt_new_ = true;
        if (t_new_name_) t_new_name_->set_editing(true);
        return true;
      }
      if (prompt_new_ && t_new_name_ && t_new_name_->handle_event(e)) {
        std::string nm = t_new_name_->value();
        if (!nm.empty() && !t_new_name_->is_editing()) {
          selected_ = nm;
          if (open_editor_) open_editor_(nm);
          prompt_new_ = false;
          t_new_name_->set_value("");
          rebuild_buttons();
          layout();
          return true;
        }
      }
      return used;
    }

    void render_content(SDL_Renderer* r) const override {
      for (auto& b : buttons_) b->render(r);
      if (b_create_) b_create_->render(r);
      if (prompt_new_ && t_new_name_) t_new_name_->render(r);
    }

    // Called by bottom button in the AssetInfo window
    void open_selected_or_first() {
      if (!info_) return;
      std::string nm = selected_;
      if (nm.empty()) {
        if (!info_->areas.empty()) nm = info_->areas.front().name;
      }
      if (!nm.empty()) {
        if (open_editor_) open_editor_(nm);
      } else {
        // No areas yet -> prompt to create
        prompt_new_ = true;
        if (t_new_name_) t_new_name_->set_editing(true);
      }
    }

  private:
    std::vector<std::unique_ptr<Button>> buttons_;
    std::unique_ptr<Button>  b_create_;
    std::unique_ptr<TextBox> t_new_name_;
    bool prompt_new_ = false;
    std::string selected_;
    std::function<void(const std::string&)> open_editor_;
};

