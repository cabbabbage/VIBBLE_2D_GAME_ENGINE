#pragma once

#include <SDL.h>
#include <memory>
#include <string>
#include <functional>
#include "../widgets.hpp"
#include "../dm_styles.hpp"

class AssetInfo;
class Input;

// Base class for a collapsible UI section in the Asset Info window
class CollapsibleSection {
  public:
    explicit CollapsibleSection(const std::string& title)
      : title_(title) {
      header_ = std::make_unique<DMButton>(title_, &DMStyles::HeaderButton(), 260, DMButton::height());
    }
    virtual ~CollapsibleSection() = default;

    virtual void set_info(const std::shared_ptr<AssetInfo>& info) { info_ = info; build(); }
    virtual void set_rect(const SDL_Rect& r) { rect_ = r; layout(); }
    const SDL_Rect& rect() const { return rect_; }
    int height() const { return DMButton::height() + (expanded_ ? content_height_ : 0) + 1; }
    bool is_expanded() const { return expanded_; }
    void set_expanded(bool e) { expanded_ = e; }
    const std::string& title() const { return title_; }

    virtual void update(const Input& /*input*/) {}

    virtual bool handle_event(const SDL_Event& e) {
      if (!header_) return false;
      bool used = header_->handle_event(e);
      if (used && e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
        expanded_ = !expanded_;
        header_->set_text(expanded_ ? title_ + " ▾" : title_ + " ▸");
      }
      return used;
    }

    virtual void render(SDL_Renderer* r) const {
      if (header_) header_->render(r);
      if (expanded_) render_content(r);
      SDL_Rect border{ rect_.x, rect_.y, rect_.w, height() };
      SDL_Color bc = DMStyles::Border();
      SDL_SetRenderDrawColor(r, bc.r, bc.g, bc.b, bc.a);
      SDL_RenderDrawRect(r, &border);
      SDL_RenderDrawLine(r, rect_.x, rect_.y + height() - 1, rect_.x + rect_.w, rect_.y + height() - 1);
    }

  protected:
    // Called when info is set/reset
    virtual void build() {}
    // Called when rect is set/reset; compute child positions; update content_height_
    virtual void layout() {
      if (header_) {
        header_->set_rect(SDL_Rect{ rect_.x, rect_.y, rect_.w, DMButton::height() });
        header_->set_text(expanded_ ? title_ + " ▾" : title_ + " ▸");
      }
      content_height_ = 0; // derived classes should set
    }
    virtual void render_content(SDL_Renderer* /*r*/) const {}

  protected:
    std::shared_ptr<AssetInfo> info_{};
    SDL_Rect rect_{0,0,0,0};
    std::unique_ptr<DMButton> header_;
    int content_height_ = 0;
    bool expanded_ = true;
    std::string title_;
};

