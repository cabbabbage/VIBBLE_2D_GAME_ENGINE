#include "dm_styles.hpp"

const DMLabelStyle &DMStyles::Label() {
  static const DMLabelStyle s{dm::FONT_PATH, 16, dm::rgba(230, 230, 230, 255)};
  return s;
}

const DMButtonStyle &DMStyles::HeaderButton() {
  static const DMButtonStyle s{
      {dm::FONT_PATH, 18, dm::rgba(220, 220, 220, 255)},
      dm::rgba(0, 0, 0, 0),
      dm::rgba(40, 40, 40, 60),
      dm::rgba(60, 60, 60, 80),
      dm::rgba(0, 0, 0, 0),
      dm::rgba(220, 220, 220, 255)};
  return s;
}

const DMButtonStyle &DMStyles::ListButton() {
  static const DMButtonStyle s{
      {dm::FONT_PATH, 16, dm::rgba(230, 230, 230, 255)},
      dm::rgba(60, 60, 60, 80),
      dm::rgba(90, 90, 90, 120),
      dm::rgba(110, 110, 110, 160),
      dm::rgba(90, 90, 90, 255),
      dm::rgba(230, 230, 230, 255)};
  return s;
}

const DMButtonStyle &DMStyles::CreateButton() {
  static const DMButtonStyle s{
      {dm::FONT_PATH, 16, dm::rgba(230, 230, 230, 255)},
      dm::rgba(70, 70, 70, 100),
      dm::rgba(110, 110, 110, 140),
      dm::rgba(130, 130, 130, 180),
      dm::rgba(90, 90, 90, 255),
      dm::rgba(230, 230, 230, 255)};
  return s;
}

const DMButtonStyle &DMStyles::DeleteButton() {
  static const DMButtonStyle s{
      {dm::FONT_PATH, 16, dm::rgba(230, 230, 230, 255)},
      dm::rgba(120, 40, 40, 120),
      dm::rgba(150, 60, 60, 160),
      dm::rgba(180, 80, 80, 200),
      dm::rgba(90, 40, 40, 255),
      dm::rgba(230, 230, 230, 255)};
  return s;
}

const DMTextBoxStyle &DMStyles::TextBox() {
  static const DMTextBoxStyle s{
      {dm::FONT_PATH, 14, dm::rgba(230, 230, 230, 255)},
      dm::rgba(50, 50, 50, 100),
      dm::rgba(90, 90, 90, 255),
      dm::rgba(130, 130, 130, 255),
      dm::rgba(240, 240, 240, 255)};
  return s;
}

const DMCheckboxStyle &DMStyles::Checkbox() {
  static const DMCheckboxStyle s{
      {dm::FONT_PATH, 14, dm::rgba(230, 230, 230, 255)},
      dm::rgba(40, 40, 40, 200),
      dm::rgba(220, 220, 220, 255),
      dm::rgba(90, 90, 90, 255)};
  return s;
}

const DMSliderStyle &DMStyles::Slider() {
  static const DMSliderStyle s{
      {dm::FONT_PATH, 14, dm::rgba(230, 230, 230, 255)},
      {dm::FONT_PATH, 14, dm::rgba(230, 230, 230, 255)},
      dm::rgba(60, 60, 60, 100),
      dm::rgba(140, 140, 140, 180),
      dm::rgba(180, 180, 180, 255),
      dm::rgba(210, 210, 210, 255),
      dm::rgba(90, 90, 90, 255),
      dm::rgba(120, 120, 120, 255)};
  return s;
}

const SDL_Color &DMStyles::PanelBG() {
  static const SDL_Color c = dm::rgba(10, 10, 10, 200);
  return c;
}

const SDL_Color &DMStyles::Border() {
  static const SDL_Color c = dm::rgba(90, 90, 90, 255);
  return c;
}
