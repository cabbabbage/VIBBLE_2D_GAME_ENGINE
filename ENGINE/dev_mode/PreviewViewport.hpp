#pragma once

#include <SDL.h>

#include <string>

class PreviewViewport {
public:
    explicit PreviewViewport(SDL_Renderer* renderer = nullptr);

    void            setRenderer(SDL_Renderer* renderer);
    SDL_Renderer*   renderer() const { return renderer_; }

    void                   setLabel(std::string label);
    const std::string&     label() const { return label_; }

    void                   setTarget(SDL_Texture* target);
    SDL_Texture*           getCurrentTarget() const { return target_; }

    bool begin();
    void end();

    void clear(Uint8 r, Uint8 g, Uint8 b, Uint8 a);

    bool present(const SDL_Rect& dst, SDL_Texture* texture = nullptr, const SDL_Rect* src = nullptr);

    void         enablePresentBlend(bool enabled);
    bool         isPresentBlendEnabled() const { return present_blend_enabled_; }
    void         setPresentBlendMode(SDL_BlendMode mode);
    SDL_BlendMode presentBlendMode() const { return present_blend_mode_; }

    int     width() const;
    int     height() const;
    Uint32  pixelFormat() const;

private:
    void invalidateCachedQuery();
    void refreshTextureInfo() const;

private:
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture*  target_   = nullptr;
    SDL_Texture*  previous_target_ = nullptr;
    bool          begin_active_    = false;

    std::string label_{};

    bool         present_blend_enabled_ = true;
    SDL_BlendMode present_blend_mode_   = SDL_BLENDMODE_BLEND;

    mutable bool   texture_info_valid_ = false;
    mutable int    cached_width_       = 0;
    mutable int    cached_height_      = 0;
    mutable Uint32 cached_format_      = 0;
};

