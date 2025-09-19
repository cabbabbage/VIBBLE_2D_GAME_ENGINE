#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include <string>

namespace dm {
inline SDL_Color rgba(Uint8 r, Uint8 g, Uint8 b, Uint8 a = 255) {
  return SDL_Color{r, g, b, a};
}
#ifdef _WIN32
constexpr const char *FONT_PATH = "C:/Windows/Fonts/segoeui.ttf";
#else
constexpr const char *FONT_PATH =
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
#endif
} // namespace dm

struct DMLabelStyle {
  std::string font_path;
  int font_size;
  SDL_Color color;
  TTF_Font *open_font() const {
    return TTF_OpenFont(font_path.c_str(), font_size);
  }
};

struct DMButtonStyle {
  DMLabelStyle label;
  SDL_Color bg;
  SDL_Color hover_bg;
  SDL_Color press_bg;
  SDL_Color border;
  SDL_Color text;
};

struct DMTextBoxStyle {
  DMLabelStyle label;
  SDL_Color bg;
  SDL_Color border;
  SDL_Color border_hover;
  SDL_Color text;
};

struct DMCheckboxStyle {
  DMLabelStyle label;
  SDL_Color box_bg;
  SDL_Color check;
  SDL_Color border;
};

struct DMSliderStyle {
  DMLabelStyle label;
  DMLabelStyle value;
  SDL_Color track_bg;
  SDL_Color track_fill;
  SDL_Color knob;
  SDL_Color knob_hover;
  SDL_Color knob_border;
  SDL_Color knob_border_hover;
};

class DMStyles {
public:
  static const DMLabelStyle &Label();
  static const DMButtonStyle &HeaderButton();
  static const DMButtonStyle &AccentButton();
  static const DMButtonStyle &ListButton();
  static const DMButtonStyle &CreateButton();
  static const DMButtonStyle &DeleteButton();
  static const DMTextBoxStyle &TextBox();
  static const DMCheckboxStyle &Checkbox();
  static const DMSliderStyle &Slider();
  static const SDL_Color &PanelBG();
  static const SDL_Color &Border();
};

// Consistent spacing tokens for dev-mode UI
struct DMSpacing {
  // Outer padding inside panels and floating boxes
  static int panel_padding();    // default 24
  // Gap between stacked sections or footer items
  static int section_gap();      // default 24
  // Gap between controls (vertical/horizontal)
  static int item_gap();         // default 12
  // Space between a widget label and its control
  static int label_gap();        // default 6
  // Smaller gap for dense grids (chips, small labels)
  static int small_gap();        // default 6
  // Space below section header before content starts
  static int header_gap();       // default 16
};
