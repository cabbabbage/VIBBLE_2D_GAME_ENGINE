#pragma once

#include <vector>
#include <SDL.h>

#include "animation_frame_variant.hpp"

class AnimationFrame {
public:
    int dx = 0;
    int dy = 0;
    bool z_resort = true;
    SDL_Color rgb{255, 255, 255, 255};
    int frame_index = -1;
    AnimationFrame* prev = nullptr;
    AnimationFrame* next = nullptr;
    bool is_last = false;
    bool is_first = false;

    // All variants for this frame
    std::vector<FrameVariant> variants;

    // Texture getters, delegated to variants
    SDL_Texture* get_base_texture(int index) const {
        return variants[index].get_base_texture();
    }

    SDL_Texture* get_foreground_texture(int index) const {
        return variants[index].get_foreground_texture();
    }

    SDL_Texture* get_background_texture(int index) const {
        return variants[index].get_background_texture();
    }

    SDL_Texture* get_shadow_mask_texture(int index) const {
        return variants[index].get_shadow_mask_texture();
    }


    std::vector<AnimationChildFrameData> children; //children are the same for every variant

    // Convenience accessors for child / hit / attack on a given variant (default first)
    const std::vector<AnimationChildFrameData>& get_children(int variant_index = 0) const {
        return children;
    }

    const animation_update::FrameHitGeometry& get_hit_geometry_single(int variant_index = 0) const {
        return variants[variant_index].get_hit_geometry();
    }

    const animation_update::FrameAttackGeometry& get_attack_geometry_single(int variant_index = 0) const {
        return variants[variant_index].get_attack_geometry();
    }
};
