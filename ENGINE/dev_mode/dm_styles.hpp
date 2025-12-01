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
}

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
  SDL_Color track_fill_active;
  SDL_Color knob;
  SDL_Color knob_hover;
  SDL_Color knob_border;
  SDL_Color knob_border_hover;
  SDL_Color knob_accent;
  SDL_Color knob_accent_border;
};

class DMStyles {
public:
  static const DMLabelStyle &Label();
  static const DMButtonStyle &HeaderButton();
  static const DMButtonStyle &AccentButton();
  static const DMButtonStyle &FooterToggleButton();
  static const DMButtonStyle &WarnButton();
  static const DMButtonStyle &ListButton();
  static const DMButtonStyle &SecondaryButton();
  static const DMButtonStyle &CreateButton();
  static const DMButtonStyle &DeleteButton();
  static const DMTextBoxStyle &TextBox();
  static const DMCheckboxStyle &Checkbox();
  static const DMSliderStyle &Slider();
  static const SDL_Color &PanelBG();
  static const SDL_Color &PanelHeader();
  static const SDL_Color &Border();
  static int CornerRadius();
  static int BevelDepth();
  static const SDL_Color &HighlightColor();
  static const SDL_Color &ShadowColor();
  static float HighlightIntensity();
  static float ShadowIntensity();
  static const SDL_Color &ButtonBaseFill();
  static const SDL_Color &ButtonHoverFill();
  static const SDL_Color &ButtonPressedFill();
  static const SDL_Color &ButtonFocusOutline();
  static const SDL_Color &TextboxBaseFill();
  static const SDL_Color &TextboxHoverFill();
  static const SDL_Color &TextboxFocusOutline();
  static const SDL_Color &TextboxHoverOutline();
  static const SDL_Color &TextboxActiveOutline();
  static const SDL_Color &TextCaretColor();
  static const SDL_Color &TextboxSelectionFill();
  static const SDL_Color &CheckboxBaseFill();
  static const SDL_Color &CheckboxHoverFill();
  static const SDL_Color &CheckboxCheckColor();
  static const SDL_Color &CheckboxOutlineColor();
  static const SDL_Color &CheckboxHoverOutline();
  static const SDL_Color &CheckboxActiveOutline();
  static const SDL_Color &CheckboxFocusOutline();
  static const SDL_Color &SliderTrackBackground();
  static const SDL_Color &SliderTrackFill();
  static const SDL_Color &SliderKnobFill();
  static const SDL_Color &SliderKnobHoverFill();
  static const SDL_Color &SliderFocusOutline();
  static const SDL_Color &SliderHoverOutline();
};

struct DMSpacing {

  static int panel_padding();

  static int section_gap();

  static int item_gap();

  static int label_gap();

  static int small_gap();

  static int header_gap();
};
