#pragma once

#include "../DockableCollapsible.hpp"

// Simple placeholder section that only shows a header and 'Coming soon'
class DummySection : public DockableCollapsible {
  public:
    explicit DummySection(const std::string& title)
      : DockableCollapsible(title, false) {}

    void layout() override {
      DockableCollapsible::layout();
      // Reserve a small placeholder height when expanded
      content_height_ = 28;
      if (header_) header_->set_text(expanded_ ? title_ + " ▲" : title_ + " ▼");
    }

    void render_content(SDL_Renderer* r) const override {
      const DMLabelStyle& s = DMStyles::Label();
      TTF_Font* f = s.open_font(); if (!f) return;
      SDL_Surface* surf = TTF_RenderUTF8_Blended(f, "(Coming soon)", s.color);
      if (surf) {
        SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
        if (tex) {
          SDL_Rect dst{ rect_.x + 24, rect_.y + DMButton::height() + 6, surf->w, surf->h };
          SDL_RenderCopy(r, tex, nullptr, &dst);
          SDL_DestroyTexture(tex);
        }
        SDL_FreeSurface(surf);
      }
      TTF_CloseFont(f);
    }
};

// Thin wrappers for individual sections/tabs from the Python tool
struct Section_Sizing      : public DummySection { Section_Sizing()      : DummySection("Sizing") {} };
struct Section_Passability : public DummySection { Section_Passability() : DummySection("Passability") {} };
struct Section_Spacing     : public DummySection { Section_Spacing()     : DummySection("Spacing") {} };
struct Section_Animations  : public DummySection { Section_Animations()  : DummySection("Animations") {} };
struct Section_ChildAssets : public DummySection { Section_ChildAssets() : DummySection("Child Assets") {} };
struct Section_Tags        : public DummySection { Section_Tags()        : DummySection("Tags") {} };
struct Section_Lighting    : public DummySection { Section_Lighting()    : DummySection("Lighting") {} };
struct Section_Json        : public DummySection { Section_Json()        : DummySection("JSON") {} };

