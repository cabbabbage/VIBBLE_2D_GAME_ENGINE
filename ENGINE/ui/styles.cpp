#include "styles.hpp"

#include <algorithm>

static inline SDL_Color make_color(Uint8 r, Uint8 g, Uint8 b, Uint8 a = 255) {
	return SDL_Color{ r, g, b, a };
}

static const SDL_Color kGold      = make_color(250,195, 73,255);
static const SDL_Color kGoldDim   = make_color(180,135, 40,255);
static const SDL_Color kTeal      = make_color( 40,110,120,255);
static const SDL_Color kSlate     = make_color( 28, 32, 36,230);
static const SDL_Color kCoal      = make_color( 12, 16, 18,255);
static const SDL_Color kNight     = make_color(  8, 12, 18,255);
static const SDL_Color kFog       = make_color(220,220,200,255);
static const SDL_Color kMist      = make_color(140,160,160,255);
static const SDL_Color kIvory     = make_color(200,200,255,200);

static const LabelStyle kLabelTitle{
#ifdef _WIN32
	"C:/Windows/Fonts/COPRGTB.TTF",
#else
	"/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf",
#endif
	74,
	kGold
};

static const LabelStyle kLabelMain{
#ifdef _WIN32
	"C:/Windows/Fonts/COPRGTB.TTF",
#else
	"/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf",
#endif
	32,
	kIvory
};

static const LabelStyle kLabelSecondary{
#ifdef _WIN32
	"C:/Windows/Fonts/GOUDOS.TTF",
#else
	"/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf",
#endif
	30,
	kGold
};

static const LabelStyle kLabelSmallMain{
#ifdef _WIN32
	"C:/Windows/Fonts/GOUDOS.TTF",
#else
	"/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf",
#endif
	30,
	kFog
};

static const LabelStyle kLabelSmallSecondary{
#ifdef _WIN32
	"C:/Windows/Fonts/GOUDOSI.TTF",
#else
	"/usr/share/fonts/truetype/dejavu/DejaVuSerif-Italic.ttf",
#endif
	30,
	kMist
};

static const LabelStyle kLabelExit{
#ifdef _WIN32
	"C:/Windows/Fonts/COPRGTB.TTF",
#else
	"/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf",
#endif
	32,
	make_color(210,170,60,255)
};

static SDL_Color brighten(SDL_Color c, int r=20, int g=20, int b=10) {
	auto clamp255 = [](int v){ return std::max(0, std::min(255, v)); };
	return make_color(
	Uint8(clamp255(int(c.r) + r)),
	Uint8(clamp255(int(c.g) + g)),
	Uint8(clamp255(int(c.b) + b)),
                       c.a
 );
}

static const ButtonStyle kMainDecoButton{
	kLabelMain,
	kSlate,
	make_color(kCoal.r, kCoal.g, kCoal.b, 200),
	kGold,
	kGoldDim,
	kTeal,
	make_color(kGold.r, kGold.g, kGold.b, 45),
	kLabelMain.color,
	brighten(kLabelMain.color)
};

static const ButtonStyle kExitDecoButton{
	kLabelExit,
	kSlate,
	make_color(kCoal.r, kCoal.g, kCoal.b, 200),
	kGold,
	kGoldDim,
	kTeal,
	make_color(kGold.r, kGold.g, kGold.b, 45),
	kLabelExit.color,
	brighten(kLabelExit.color)
};

const SDL_Color& Styles::Gold()      { return kGold; }
const SDL_Color& Styles::GoldDim()   { return kGoldDim; }
const SDL_Color& Styles::Teal()      { return kTeal; }
const SDL_Color& Styles::Slate()     { return kSlate; }
const SDL_Color& Styles::Coal()      { return kCoal; }
const SDL_Color& Styles::Night()     { return kNight; }
const SDL_Color& Styles::Fog()       { return kFog; }
const SDL_Color& Styles::Mist()      { return kMist; }
const SDL_Color& Styles::Ivory()     { return kIvory; }

const LabelStyle& Styles::LabelTitle()          { return kLabelTitle; }
const LabelStyle& Styles::LabelMain()           { return kLabelMain; }
const LabelStyle& Styles::LabelSecondary()      { return kLabelSecondary; }
const LabelStyle& Styles::LabelSmallMain()      { return kLabelSmallMain; }
const LabelStyle& Styles::LabelSmallSecondary() { return kLabelSmallSecondary; }
const LabelStyle& Styles::LabelExit()           { return kLabelExit; }

const ButtonStyle& Styles::MainDecoButton()     { return kMainDecoButton; }
const ButtonStyle& Styles::ExitDecoButton()     { return kExitDecoButton; }
