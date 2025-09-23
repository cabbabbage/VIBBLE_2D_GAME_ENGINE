#include "dm_styles.hpp"

namespace {
const SDL_Color kTextPrimary       = dm::rgba(226, 232, 240, 255);
const SDL_Color kTextSecondary     = dm::rgba(203, 213, 225, 255);

const SDL_Color kPanelBackground   = dm::rgba(15, 23, 42, 235);
const SDL_Color kPanelHeader       = dm::rgba(30, 41, 59, 240);
const SDL_Color kPanelBorder       = dm::rgba(71, 85, 105, 255);

const SDL_Color kHeaderBg          = dm::rgba(30, 41, 59, 235);
const SDL_Color kHeaderHover       = dm::rgba(46, 64, 94, 245);
const SDL_Color kHeaderPress       = dm::rgba(24, 34, 53, 245);
const SDL_Color kHeaderText        = kTextPrimary;

const SDL_Color kAccentBg          = dm::rgba(37, 99, 235, 235);
const SDL_Color kAccentHover       = dm::rgba(59, 130, 246, 245);
const SDL_Color kAccentPress       = dm::rgba(29, 78, 216, 235);
const SDL_Color kAccentBorder      = dm::rgba(30, 64, 175, 255);
const SDL_Color kAccentText        = dm::rgba(240, 249, 255, 255);

const SDL_Color kListBg            = dm::rgba(20, 30, 49, 210);
const SDL_Color kListHover         = dm::rgba(31, 45, 70, 230);
const SDL_Color kListPress         = dm::rgba(41, 56, 85, 240);
const SDL_Color kListBorder        = dm::rgba(52, 70, 105, 255);
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

const SDL_Color kTextboxBg         = dm::rgba(13, 23, 38, 235);
const SDL_Color kTextboxBorder     = dm::rgba(48, 64, 96, 255);
const SDL_Color kTextboxBorderHot  = dm::rgba(73, 103, 151, 255);
const SDL_Color kTextboxText       = kTextPrimary;

const SDL_Color kCheckboxBg        = dm::rgba(20, 32, 52, 235);
const SDL_Color kCheckboxBorder    = dm::rgba(57, 81, 123, 255);
const SDL_Color kCheckboxCheck     = dm::rgba(59, 130, 246, 255);

const SDL_Color kSliderTrack       = dm::rgba(21, 30, 50, 220);
const SDL_Color kSliderFill        = dm::rgba(59, 130, 246, 240);
const SDL_Color kSliderKnob        = dm::rgba(226, 232, 240, 255);
const SDL_Color kSliderKnobHover   = dm::rgba(186, 230, 253, 255);
const SDL_Color kSliderKnobBorder  = dm::rgba(59, 130, 246, 255);
const SDL_Color kSliderKnobBorderHover = dm::rgba(96, 165, 250, 255);
} // namespace

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
      kCheckboxBorder};
  return s;
}

const DMSliderStyle &DMStyles::Slider() {
  static const DMSliderStyle s{
      {dm::FONT_PATH, 14, kTextSecondary},
      {dm::FONT_PATH, 14, kTextPrimary},
      kSliderTrack,
      kSliderFill,
      kSliderKnob,
      kSliderKnobHover,
      kSliderKnobBorder,
      kSliderKnobBorderHover};
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

int DMSpacing::panel_padding() { return 24; }
int DMSpacing::section_gap()   { return 24; }
int DMSpacing::item_gap()      { return 12; }
int DMSpacing::label_gap()    { return 6; }
int DMSpacing::small_gap()     { return 6; }
int DMSpacing::header_gap()    { return 16; }
