// === File: parallax.hpp ===
#pragma once
#include <SDL.h>

class Parallax {
public:
    Parallax(int screenWidth, int screenHeight);

    // Set player/world reference point
    void setReference(int px, int py);

    // Convert world → screen coordinates
    SDL_Point apply(int ax, int ay) const;

    // Convert screen → world coordinates
    SDL_Point inverse(int screen_x, int screen_y) const;

    // Configure parallax effect strength
    void setParallaxMax(float maxX, float maxY);

    // Enable/disable parallax effect
    void setDisabled(bool flag);
    bool isDisabled() const;

private:
    int screenWidth_;
    int screenHeight_;
    float halfWidth_;
    float halfHeight_;

    int lastPx_;
    int lastPy_;

    float parallaxMaxX_;
    float parallaxMaxY_;

    bool disabled_;
};
