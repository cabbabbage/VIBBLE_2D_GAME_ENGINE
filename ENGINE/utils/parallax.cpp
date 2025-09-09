#include "parallax.hpp"
#include "asset/Asset.hpp"
#include <algorithm>
#include <cmath>
Parallax::Parallax(int screenWidth, int screenHeight)
: screenWidth_(screenWidth),
screenHeight_(screenHeight),
halfWidth_(screenWidth > 0 ? screenWidth * 0.5f : 1.0f),
halfHeight_(screenHeight > 0 ? screenHeight * 0.5f : 1.0f),
lastPx_(0),
lastPy_(0),
parallaxMaxX_(0.0f),
parallaxMaxY_(0.0f),
disabled_(true)
{}

void Parallax::setReference(int px, int py) {
	lastPx_ = px;
	lastPy_ = py;
}

SDL_Point Parallax::apply(int ax, int ay) const {
	float world_dx = float(ax - lastPx_);
	float world_dy = float(ay - lastPy_);
	float ndx = world_dx / halfWidth_;
	float ndy = world_dy / halfHeight_;
	float offX = ndx * parallaxMaxX_;
	float offY = ndy * parallaxMaxY_;
	int screen_x = static_cast<int>(world_dx + halfWidth_ + offX);
	int screen_y = static_cast<int>(world_dy + halfHeight_ + offY);
	return {screen_x, screen_y};
}

SDL_Point Parallax::inverse(int screen_x, int screen_y) const {
	float dx = float(screen_x) - halfWidth_;
	float dy = float(screen_y) - halfHeight_;
	float ndy = dy / (halfHeight_ != 0.0f ? halfHeight_ : 1.0f);
	float offX = parallaxMaxX_ * ndy;
	float offY = parallaxMaxY_ * (dy / (halfHeight_ != 0.0f ? halfHeight_ : 1.0f));
	float world_dx = dx - offX;
	float world_dy = dy - offY;
	int world_x = int(std::lround(lastPx_ + world_dx));
	int world_y = int(std::lround(lastPy_ + world_dy));
	return {world_x, world_y};
}

void Parallax::setParallaxMax(float maxX, float maxY) {
	parallaxMaxX_ = std::max(0.0f, maxX);
	parallaxMaxY_ = std::max(0.0f, maxY);
}

void Parallax::setDisabled(bool flag) {
	disabled_ = flag;
}

bool Parallax::isDisabled() const {
	return disabled_;
}

void Parallax::update_screen_position(Asset& a) const {
	SDL_Point p = apply(a.pos.x, a.pos.y);
	a.set_screen_position(p.x, p.y);
}
