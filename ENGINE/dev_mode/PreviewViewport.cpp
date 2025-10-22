#include "PreviewViewport.hpp"

#include <utility>

PreviewViewport::PreviewViewport(SDL_Renderer* renderer) : renderer_(renderer) {}

void PreviewViewport::setRenderer(SDL_Renderer* renderer) {
    if (renderer_ == renderer) {
        return;
    }
    end();
    renderer_ = renderer;
}

void PreviewViewport::setLabel(std::string label) { label_ = std::move(label); }

void PreviewViewport::setTarget(SDL_Texture* target) {
    if (target_ == target) {
        return;
    }
    end();
    target_ = target;
    invalidateCachedQuery();
}

bool PreviewViewport::begin() {
    if (begin_active_) {
        return true;
    }
    if (!renderer_ || !target_) {
        return false;
    }
    previous_target_ = SDL_GetRenderTarget(renderer_);
    if (SDL_SetRenderTarget(renderer_, target_) != 0) {
        previous_target_ = nullptr;
        return false;
    }
    begin_active_ = true;
    return true;
}

void PreviewViewport::end() {
    if (!begin_active_ || !renderer_) {
        begin_active_ = false;
        previous_target_ = nullptr;
        return;
    }
    SDL_SetRenderTarget(renderer_, previous_target_);
    begin_active_ = false;
    previous_target_ = nullptr;
}

void PreviewViewport::clear(Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
    if (!begin_active_ && !begin()) {
        return;
    }
    if (!renderer_) {
        return;
    }
    SDL_SetRenderDrawColor(renderer_, r, g, b, a);
    SDL_RenderClear(renderer_);
}

bool PreviewViewport::present(const SDL_Rect& dst, SDL_Texture* texture, const SDL_Rect* src) {
    SDL_Texture* target = texture ? texture : target_;
    if (!renderer_ || !target || dst.w <= 0 || dst.h <= 0) {
        return false;
    }

    SDL_BlendMode saved_mode = SDL_BLENDMODE_NONE;
    SDL_GetTextureBlendMode(target, &saved_mode);

    SDL_BlendMode desired = present_blend_enabled_ ? present_blend_mode_ : SDL_BLENDMODE_NONE;
    if (desired != saved_mode) {
        SDL_SetTextureBlendMode(target, desired);
    }

    bool success = (SDL_RenderCopy(renderer_, target, src, &dst) == 0);

    if (desired != saved_mode) {
        SDL_SetTextureBlendMode(target, saved_mode);
    }

    return success;
}

void PreviewViewport::enablePresentBlend(bool enabled) { present_blend_enabled_ = enabled; }

void PreviewViewport::setPresentBlendMode(SDL_BlendMode mode) { present_blend_mode_ = mode; }

int PreviewViewport::width() const {
    refreshTextureInfo();
    return cached_width_;
}

int PreviewViewport::height() const {
    refreshTextureInfo();
    return cached_height_;
}

Uint32 PreviewViewport::pixelFormat() const {
    refreshTextureInfo();
    return cached_format_;
}

void PreviewViewport::invalidateCachedQuery() {
    texture_info_valid_ = false;
    cached_width_       = 0;
    cached_height_      = 0;
    cached_format_      = 0;
}

void PreviewViewport::refreshTextureInfo() const {
    if (texture_info_valid_) {
        return;
    }

    cached_width_  = 0;
    cached_height_ = 0;
    cached_format_ = 0;

    if (target_) {
        Uint32 format = 0;
        int    access = 0;
        int    w      = 0;
        int    h      = 0;
        if (SDL_QueryTexture(target_, &format, &access, &w, &h) == 0) {
            cached_width_  = w;
            cached_height_ = h;
            cached_format_ = format;
        }
    }

    texture_info_valid_ = true;
}

