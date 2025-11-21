#include "dm_styles.hpp"

namespace {
const SDL_Color kTextPrimary       = dm::rgba(226, 232, 240, 255);
const SDL_Color kTextSecondary     = dm::rgba(203, 213, 225, 255);

const SDL_Color kHighlightWhite    = dm::rgba(248, 250, 252, 255);

const SDL_Color kPanelBackground   = dm::rgba(15, 23, 42, 235);
const SDL_Color kPanelHeader       = dm::rgba(30, 41, 59, 240);
const SDL_Color kNeutralBorder     = dm::rgba(94, 109, 132, 255);
const SDL_Color kPanelBorder       = kNeutralBorder;
const SDL_Color kHighlightColor    = kHighlightWhite;
const SDL_Color kShadowColor       = dm::rgba(9, 14, 25, 255);
constexpr float kHighlightIntensity = 0.72f;
constexpr float kShadowIntensity    = 0.66f;
constexpr int kCornerRadius         = 10;
constexpr int kBevelDepth           = 1;

const SDL_Color kHeaderBg          = dm::rgba(52, 71, 105, 240);
const SDL_Color kHeaderHover       = dm::rgba(68, 92, 130, 245);
const SDL_Color kHeaderPress       = dm::rgba(40, 57, 86, 245);
const SDL_Color kHeaderText        = kTextPrimary;

const SDL_Color kAccentOrange           = dm::rgba(249, 115, 22, 240);
const SDL_Color kAccentOrangeHover      = dm::rgba(251, 146, 60, 245);
const SDL_Color kAccentOrangeStrong     = dm::rgba(194, 65, 12, 255);
const SDL_Color kAccentBorder           = kAccentOrangeStrong;
const SDL_Color kAccentBg               = kAccentOrange;
const SDL_Color kAccentHover            = kAccentOrangeHover;
const SDL_Color kAccentPress            = dm::rgba(194, 65, 12, 240);
const SDL_Color kAccentText             = kHighlightWhite;

const SDL_Color kFooterToggleBg     = dm::rgba(220, 200, 115, 220);
const SDL_Color kFooterToggleHover  = dm::rgba(253, 224, 71, 235);
const SDL_Color kFooterTogglePress  = dm::rgba(217, 119, 6, 235);
const SDL_Color kFooterToggleBorder = dm::rgba(161, 98, 7, 255);
const SDL_Color kFooterToggleText   = kHighlightWhite;

const SDL_Color kWarnBg            = dm::rgba(234, 179, 8, 235);
const SDL_Color kWarnHover         = dm::rgba(250, 204, 21, 245);
const SDL_Color kWarnPress         = dm::rgba(202, 138, 4, 235);
const SDL_Color kWarnBorder        = dm::rgba(161, 98, 7, 255);
const SDL_Color kWarnText          = dm::rgba(30, 30, 30, 255);

const SDL_Color kListBg            = dm::rgba(45, 64, 96, 225);
const SDL_Color kListHover         = dm::rgba(60, 82, 118, 240);
const SDL_Color kListPress         = dm::rgba(38, 54, 82, 240);
const SDL_Color kListBorder        = kNeutralBorder;
const SDL_Color kListText          = dm::rgba(215, 224, 244, 255);

const SDL_Color kCreateBg          = dm::rgba(34, 139, 116, 230);
const SDL_Color kCreateHover       = dm::rgba(52, 167, 140, 240);
const SDL_Color kCreatePress       = dm::rgba(28, 117, 97, 230);
const SDL_Color kCreateBorder      = dm::rgba(30, 120, 100, 255);
const SDL_Color kCreateText        = dm::rgba(230, 252, 244, 255);

const SDL_Color kDeleteBg          = dm::rgba(185, 28, 28, 235);
const SDL_Color kDeleteHover       = dm::rgba(220, 38, 38, 245);
const SDL_Color kDeletePress       = dm::rgba(153, 27, 27, 235);
const SDL_Color kDeleteBorder      = dm::rgba(127, 29, 29, 255);
const SDL_Color kDeleteText        = dm::rgba(254, 226, 226, 255);

const SDL_Color kTextboxBg             = dm::rgba(13, 23, 38, 235);
const SDL_Color kTextboxBgHover        = dm::rgba(18, 32, 52, 240);
const SDL_Color kTextboxBorder         = dm::rgba(48, 64, 96, 255);
const SDL_Color kTextboxBorderHot      = dm::rgba(73, 103, 151, 255);
const SDL_Color kTextboxBorderPreview  = dm::rgba(248, 250, 252, 235);
const SDL_Color kTextboxBorderActive   = dm::rgba(245, 158, 11, 255);
const SDL_Color kTextboxCaret          = dm::rgba(251, 191, 36, 255);
const SDL_Color kTextboxSelection      = dm::rgba(245, 158, 11, 96);
const SDL_Color kTextboxText           = kTextPrimary;

const SDL_Color kCheckboxBg            = dm::rgba(20, 32, 52, 235);
const SDL_Color kCheckboxBgHover       = dm::rgba(28, 44, 72, 240);
const SDL_Color kCheckboxOutline       = dm::rgba(57, 81, 123, 255);
const SDL_Color kCheckboxCheck         = dm::rgba(251, 146, 60, 255);
const SDL_Color kCheckboxFocus         = dm::rgba(96, 165, 250, 255);
const SDL_Color kCheckboxHoverOutline  = dm::rgba(248, 250, 252, 255);
const SDL_Color kCheckboxActiveOutline = dm::rgba(234, 88, 12, 255);

const SDL_Color kSliderTrack           = dm::rgba(21, 30, 50, 220);
const SDL_Color kSliderFill            = dm::rgba(148, 163, 184, 200);
const SDL_Color kSliderFillActive      = dm::rgba(249, 115, 22, 235);
const SDL_Color kSliderKnob            = dm::rgba(226, 232, 240, 255);
const SDL_Color kSliderKnobHover       = dm::rgba(241, 245, 249, 255);
const SDL_Color kSliderKnobBorder      = dm::rgba(148, 163, 184, 255);
const SDL_Color kSliderKnobBorderHover = dm::rgba(248, 250, 252, 255);
const SDL_Color kSliderKnobAccent      = dm::rgba(251, 146, 60, 255);
const SDL_Color kSliderKnobAccentBorder = dm::rgba(194, 65, 12, 255);
const SDL_Color kSliderFocusOutline    = dm::rgba(249, 115, 22, 255);
const SDL_Color kSliderHoverOutline    = kSliderKnobBorderHover;

const SDL_Color kButtonFocusOutline = kAccentOrangeStrong;
const SDL_Color kButtonBaseFill     = kListBg;
const SDL_Color kButtonHoverFill    = kListHover;
const SDL_Color kButtonPressFill    = kListPress;
}

const DMLabelStyle &DMStyles::Label() {
  static const DMLabelStyle s{dm::FONT_PATH, 16, kTextPrimary};
  return s;
}

const DMButtonStyle &DMStyles::HeaderButton() {
  static const DMButtonStyle s{
      {dm::FONT_PATH, 18, kHeaderText},
      kHeaderBg,
      kHeaderHover,
      kHeaderPress,
      kPanelBorder,
      kHeaderText};
  return s;
}

const DMButtonStyle &DMStyles::AccentButton() {
  static const DMButtonStyle s{
      {dm::FONT_PATH, 18, kAccentText},
      kAccentBg,
      kAccentHover,
      kAccentPress,
      kAccentBorder,
      kAccentText};
  return s;
}

const DMButtonStyle &DMStyles::FooterToggleButton() {
  static const DMButtonStyle s{
      {dm::FONT_PATH, 18, kFooterToggleText},
      kFooterToggleBg,
      kFooterToggleHover,
      kFooterTogglePress,
      kFooterToggleBorder,
      kFooterToggleText};
  return s;
}

const DMButtonStyle &DMStyles::WarnButton() {
  static const DMButtonStyle s{
      {dm::FONT_PATH, 18, kWarnText},
      kWarnBg,
      kWarnHover,
      kWarnPress,
      kWarnBorder,
      kWarnText};
  return s;
}

const DMButtonStyle &DMStyles::ListButton() {
  static const DMButtonStyle s{
      {dm::FONT_PATH, 16, kListText},
      kListBg,
      kListHover,
      kListPress,
      kListBorder,
      kListText};
  return s;
}

const DMButtonStyle &DMStyles::SecondaryButton() {
  return DMStyles::ListButton();
}

const DMButtonStyle &DMStyles::CreateButton() {
  static const DMButtonStyle s{
      {dm::FONT_PATH, 16, kCreateText},
      kCreateBg,
      kCreateHover,
      kCreatePress,
      kCreateBorder,
      kCreateText};
  return s;
}

const DMButtonStyle &DMStyles::DeleteButton() {
  static const DMButtonStyle s{
      {dm::FONT_PATH, 16, kDeleteText},
      kDeleteBg,
      kDeleteHover,
      kDeletePress,
      kDeleteBorder,
      kDeleteText};
  return s;
}

const DMTextBoxStyle &DMStyles::TextBox() {
  static const DMTextBoxStyle s{
      {dm::FONT_PATH, 14, kTextSecondary},
      kTextboxBg,
      kTextboxBorder,
      kTextboxBorderHot,
      kTextboxText};
  return s;
}

const DMCheckboxStyle &DMStyles::Checkbox() {
  static const DMCheckboxStyle s{
      {dm::FONT_PATH, 14, kTextSecondary},
      kCheckboxBg,
      kCheckboxCheck,
      kCheckboxOutline};
  return s;
}

const DMSliderStyle &DMStyles::Slider() {
  static const DMSliderStyle s{
      {dm::FONT_PATH, 14, kTextSecondary},
      {dm::FONT_PATH, 14, kTextPrimary},
      kSliderTrack,
      kSliderFill,
      kSliderFillActive,
      kSliderKnob,
      kSliderKnobHover,
      kSliderKnobBorder,
      kSliderKnobBorderHover,
      kSliderKnobAccent,
      kSliderKnobAccentBorder};
  return s;
}

const SDL_Color &DMStyles::PanelBG() {
  static const SDL_Color c = kPanelBackground;
  return c;
}

const SDL_Color &DMStyles::PanelHeader() {
  static const SDL_Color c = kPanelHeader;
  return c;
}

const SDL_Color &DMStyles::Border() {
  static const SDL_Color c = kPanelBorder;
  return c;
}

int DMStyles::CornerRadius() { return kCornerRadius; }

int DMStyles::BevelDepth() { return kBevelDepth; }

const SDL_Color &DMStyles::HighlightColor() {
  static const SDL_Color c = kHighlightColor;
  return c;
}

const SDL_Color &DMStyles::ShadowColor() {
  static const SDL_Color c = kShadowColor;
  return c;
}

float DMStyles::HighlightIntensity() { return kHighlightIntensity; }

float DMStyles::ShadowIntensity() { return kShadowIntensity; }

const SDL_Color &DMStyles::ButtonBaseFill() {
  static const SDL_Color c = kButtonBaseFill;
  return c;
}

const SDL_Color &DMStyles::ButtonHoverFill() {
  static const SDL_Color c = kButtonHoverFill;
  return c;
}

const SDL_Color &DMStyles::ButtonPressedFill() {
  static const SDL_Color c = kButtonPressFill;
  return c;
}

const SDL_Color &DMStyles::ButtonFocusOutline() {
  static const SDL_Color c = kButtonFocusOutline;
  return c;
}

const SDL_Color &DMStyles::TextboxBaseFill() {
  static const SDL_Color c = kTextboxBg;
  return c;
}

const SDL_Color &DMStyles::TextboxHoverFill() {
  static const SDL_Color c = kTextboxBgHover;
  return c;
}

const SDL_Color &DMStyles::TextboxFocusOutline() {
  static const SDL_Color c = kTextboxBorderHot;
  return c;
}

const SDL_Color &DMStyles::TextboxHoverOutline() {
  static const SDL_Color c = kTextboxBorderPreview;
  return c;
}

const SDL_Color &DMStyles::TextboxActiveOutline() {
  static const SDL_Color c = kTextboxBorderActive;
  return c;
}

const SDL_Color &DMStyles::TextCaretColor() {
  static const SDL_Color c = kTextboxCaret;
  return c;
}

const SDL_Color &DMStyles::TextboxSelectionFill() {
  static const SDL_Color c = kTextboxSelection;
  return c;
}

const SDL_Color &DMStyles::CheckboxBaseFill() {
  static const SDL_Color c = kCheckboxBg;
  return c;
}

const SDL_Color &DMStyles::CheckboxHoverFill() {
  static const SDL_Color c = kCheckboxBgHover;
  return c;
}

const SDL_Color &DMStyles::CheckboxCheckColor() {
  static const SDL_Color c = kCheckboxCheck;
  return c;
}

const SDL_Color &DMStyles::CheckboxOutlineColor() {
  static const SDL_Color c = kCheckboxOutline;
  return c;
}

const SDL_Color &DMStyles::CheckboxHoverOutline() {
  static const SDL_Color c = kCheckboxHoverOutline;
  return c;
}

const SDL_Color &DMStyles::CheckboxActiveOutline() {
  static const SDL_Color c = kCheckboxActiveOutline;
  return c;
}

const SDL_Color &DMStyles::CheckboxFocusOutline() {
  static const SDL_Color c = kCheckboxFocus;
  return c;
}

const SDL_Color &DMStyles::SliderTrackBackground() {
  static const SDL_Color c = kSliderTrack;
  return c;
}

const SDL_Color &DMStyles::SliderTrackFill() {
  static const SDL_Color c = kSliderFill;
  return c;
}

const SDL_Color &DMStyles::SliderKnobFill() {
  static const SDL_Color c = kSliderKnob;
  return c;
}

const SDL_Color &DMStyles::SliderKnobHoverFill() {
  static const SDL_Color c = kSliderKnobHover;
  return c;
}

const SDL_Color &DMStyles::SliderFocusOutline() {
  static const SDL_Color c = kSliderFocusOutline;
  return c;
}

const SDL_Color &DMStyles::SliderHoverOutline() {
  static const SDL_Color c = kSliderHoverOutline;
  return c;
}

int DMSpacing::panel_padding() { return 24; }
int DMSpacing::section_gap()   { return 24; }
int DMSpacing::item_gap()      { return 12; }
int DMSpacing::label_gap()    { return 6; }
int DMSpacing::small_gap()     { return 6; }
int DMSpacing::header_gap()    { return 16; }
