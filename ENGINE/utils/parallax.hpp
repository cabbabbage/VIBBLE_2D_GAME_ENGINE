
#pragma once
#include <SDL.h>

class Asset; // fwd decl to avoid heavy includes

class Parallax {
public:
    Parallax(int screenWidth, int screenHeight);

    
    void setReference(int px, int py);

    
    SDL_Point apply(int ax, int ay) const;

    
    SDL_Point inverse(int screen_x, int screen_y) const;

    
    void setParallaxMax(float maxX, float maxY);

    
    void setDisabled(bool flag);
    bool isDisabled() const;

    // Compute and set asset screen position from its world position
    void update_screen_position(Asset& a) const;

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
