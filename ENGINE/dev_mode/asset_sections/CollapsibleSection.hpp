#pragma once

#include <SDL.h>
#include <memory>
#include <string>
#include <functional>
#include <algorithm>
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
      expanded_ = false; // start collapsed
    }
    virtual ~CollapsibleSection() = default;

    virtual void set_info(const std::shared_ptr<AssetInfo>& info) { info_ = info; build(); }
    virtual void set_rect(const SDL_Rect& r) {
      rect_ = r;
      layout();
      recalc_scroll_limits();
    }
    const SDL_Rect& rect() const { return rect_; }
    int height() const {
      int vis = expanded_ ? std::min(content_height_, visible_height_) : 0;
      return DMButton::height() + vis + 1;
    }
    bool is_expanded() const { return expanded_; }
    void set_expanded(bool e) { expanded_ = e; }
    const std::string& title() const { return title_; }

    virtual void update(const Input& /*input*/) {}

    virtual bool handle_event(const SDL_Event& e) {
      if (!header_) return false;
      bool used = header_->handle_event(e);
      if (used && e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
        expanded_ = !expanded_;
        header_->set_text(expanded_ ? title_ + " \xE2\x96\xB2" : title_ + " \xE2\x96\xBC");
      }
      if (expanded_ && e.type == SDL_MOUSEWHEEL) {
        int mx, my;
        SDL_GetMouseState(&mx, &my);
        SDL_Rect area{ rect_.x, rect_.y + DMButton::height(), rect_.w, std::min(content_height_, visible_height_) };
        if (mx >= area.x && mx < area.x + area.w && my >= area.y && my < area.y + area.h) {
          scroll_ -= e.wheel.y * 40;
          if (scroll_ < 0) scroll_ = 0;
          if (scroll_ > max_scroll_) scroll_ = max_scroll_;
          layout();
          recalc_scroll_limits();
          used = true;
        }
      }
      return used;
    }

    virtual void render(SDL_Renderer* r) const {
      if (header_) header_->render(r);
      if (expanded_) {
        SDL_Rect clip{ rect_.x, rect_.y + DMButton::height(), rect_.w,
                       std::min(content_height_, visible_height_) };
        SDL_RenderSetClipRect(r, &clip);
        render_content(r);
        SDL_RenderSetClipRect(r, nullptr);
      }
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
        header_->set_text(expanded_ ? title_ + " \xE2\x96\xB2" : title_ + " \xE2\x96\xBC");
      }
      content_height_ = 0; // derived classes should set
    }
    virtual void render_content(SDL_Renderer* /*r*/) const {}

  protected:
    void recalc_scroll_limits() {
      max_scroll_ = std::max(0, content_height_ - visible_height_);
      if (scroll_ > max_scroll_) scroll_ = max_scroll_;
    }

    std::shared_ptr<AssetInfo> info_{};
    SDL_Rect rect_{0,0,0,0};
    std::unique_ptr<DMButton> header_;
    int content_height_ = 0;
    int visible_height_ = 300;
    int scroll_ = 0;
    int max_scroll_ = 0;
    bool expanded_ = false;
    std::string title_;
};


