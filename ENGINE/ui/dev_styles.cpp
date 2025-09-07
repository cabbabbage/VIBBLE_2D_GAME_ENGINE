#include "dev_styles.hpp"
#include "ui/slider.hpp"

namespace {
    inline SDL_Color rgba(Uint8 r, Uint8 g, Uint8 b, Uint8 a=255) {
        SDL_Color c{r,g,b,a}; return c;
    }

    // Typography
    static LabelStyle kBtnLabel{
    #ifdef _WIN32
        "C:/Windows/Fonts/segoeui.ttf",
    #else
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    #endif
        20,
        rgba(31,41,55,255) // gray-800
    };

    static LabelStyle kBtnLabelSecondary{
    #ifdef _WIN32
        "C:/Windows/Fonts/segoeui.ttf",
    #else
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    #endif
        20,
        rgba(75,85,99,255) // gray-600
    };

    // Button styles: flat/neutral
    static const ButtonStyle kPrimaryButton{
        /*label       */ kBtnLabel,
        /*fill_base   */ rgba(243,244,246,255),   // gray-100
        /*fill_top    */ rgba(255,255,255,200),   // subtle top sheen
        /*outline     */ rgba(148,163,184,255),   // slate-400
        /*outline_dim */ rgba(203,213,225,255),   // slate-300
        /*accent      */ rgba(59,130,246,80),     // blue (very low alpha accents)
        /*glow        */ rgba(59,130,246,30),     // soft hover glow
        /*text_normal */ rgba(31,41,55,255),
        /*text_hover  */ rgba(17,24,39,255)
    };

    static const ButtonStyle kSecondaryButton{
        /*label       */ kBtnLabelSecondary,
        /*fill_base   */ rgba(249,250,251,255),   // gray-50
        /*fill_top    */ rgba(255,255,255,180),
        /*outline     */ rgba(209,213,219,255),   // gray-300
        /*outline_dim */ rgba(229,231,235,255),   // gray-200
        /*accent      */ rgba(99,102,241,60),     // indigo low alpha
        /*glow        */ rgba(0,0,0,0),
        /*text_normal */ rgba(75,85,99,255),
        /*text_hover  */ rgba(55,65,81,255)
    };

    // Slider style
    static const SliderStyle kDefaultSlider{
        /*frame_normal     */ rgba(203,213,225,255),  // slate-300
        /*frame_hover      */ rgba(148,163,184,255),  // slate-400
        /*track_bg         */ rgba(243,244,246,255),  // gray-100
        /*track_fill       */ rgba(59,130,246,255),   // blue-500
        /*knob_fill        */ rgba(255,255,255,255),
        /*knob_fill_hover  */ rgba(248,250,252,255),  // slate-50
        /*knob_frame       */ rgba(203,213,225,255),
        /*knob_frame_hover */ rgba(148,163,184,255),
        /*label_style      */ TextStyle{
    #ifdef _WIN32
            "C:/Windows/Fonts/segoeui.ttf",
    #else
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    #endif
            16, rgba(75,85,99,255)
        },
        /*value_style      */ TextStyle{
    #ifdef _WIN32
            "C:/Windows/Fonts/segoeui.ttf",
    #else
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    #endif
            16, rgba(31,41,55,255)
        }
    };

    static const SDL_Color kPanelBG = rgba(250,250,251,220); // light translucent
    static const SDL_Color kOutline  = rgba(203,213,225,255);
    static const SDL_Color kAccent   = rgba(59,130,246,255);
}

const ButtonStyle& DevStyles::PrimaryButton()   { return kPrimaryButton; }
const ButtonStyle& DevStyles::SecondaryButton() { return kSecondaryButton; }
const SliderStyle& DevStyles::DefaultSlider()   { return kDefaultSlider; }
const SDL_Color&  DevStyles::PanelBG()          { return kPanelBG; }
const SDL_Color&  DevStyles::Outline()          { return kOutline; }
const SDL_Color&  DevStyles::Accent()           { return kAccent; }

