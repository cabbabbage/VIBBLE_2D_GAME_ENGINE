#pragma once

#include <SDL.h>

#include <optional>
#include <vector>

class Assets;
class Asset;

namespace world {
class Chunk;
} // namespace world

namespace lighting {

// Pre-computed draw inputs for lighting preprocessing. Aggregates all static
// primitives that participate in the base/min/max passes for a chunk. The
// implementation intentionally keeps the interface light-weight so the
// pipeline can be driven from both tooling and runtime code.
class PreloadInputs {
public:
    struct TextureDraw {
        SDL_Texture* texture = nullptr;
        SDL_Rect     src{0, 0, 0, 0};
        SDL_Rect     dst{0, 0, 0, 0};
        SDL_BlendMode blend = SDL_BLENDMODE_BLEND;
    };

    PreloadInputs(SDL_Renderer* renderer, Assets* assets, world::Chunk& chunk);

    SDL_Renderer* renderer() const { return renderer_; }
    Assets*       assets() const { return assets_; }
    world::Chunk& chunk() const { return *chunk_; }

    const SDL_Rect& chunkBounds() const { return chunk_bounds_; }
    SDL_Point       targetSize() const { return target_size_; }
    Uint32          runtimePixelFormat() const { return pixel_format_; }

    const std::vector<TextureDraw>& backgroundDraws() const { return backgrounds_; }
    const std::vector<TextureDraw>& tileDraws() const { return tiles_; }
    const std::vector<TextureDraw>& staticAssetDraws() const { return assets_draws_; }
    const std::vector<TextureDraw>& staticLightDraws() const { return static_light_draws_; }

    SDL_BlendMode runtimeLightBlendMode() const { return light_blend_mode_; }
    static SDL_BlendMode computeRuntimeLightBlendMode();

    void disableScreenLightAndMovingLights();
    void restoreRuntimeLighting();

private:
    void resolveChunkBounds();
    void stageBackgrounds();
    void stageTiles();
    void stageAssets();
    void stageStaticLights();
    void captureBlendConfig();

private:
    SDL_Renderer* renderer_ = nullptr;
    Assets*       assets_   = nullptr;
    world::Chunk* chunk_    = nullptr;

    SDL_Rect chunk_bounds_{0, 0, 0, 0};
    SDL_Point target_size_{1, 1};
    Uint32    pixel_format_    = SDL_PIXELFORMAT_RGBA32;
    SDL_BlendMode light_blend_mode_ = SDL_BLENDMODE_BLEND;

    std::vector<TextureDraw> backgrounds_{};
    std::vector<TextureDraw> tiles_{};
    std::vector<TextureDraw> assets_draws_{};
    std::vector<TextureDraw> static_light_draws_{};

    struct LightingBackup {
        bool  valid                    = false;
        bool  has_dynamic_overlay      = false;
        bool  lighting_active          = false;
        bool  moving_light_occupied    = false;
        float current_strength         = 1.0f;
        bool  runtime_average_valid    = false;
        float runtime_average_strength = 1.0f;
    } backup_{};
};

} // namespace lighting

