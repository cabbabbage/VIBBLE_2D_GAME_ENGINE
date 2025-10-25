#pragma once

#include <SDL.h>

#include <vector>
#include <cstddef>

class Assets;
class camera;
class Asset;

namespace runtime_lighting {

struct AssetLight {
    Asset*   asset            = nullptr;
    SDL_Rect asset_rect{0, 0, 0, 0};
    int      base_width       = 0;
    int      base_height      = 0;
    bool     flipped          = false;
    float    asset_base_scale = 1.0f;
};

struct RuntimeLightingFrame {
    struct Sample {
<<<<<<< ours
        int   chunk_i    = 0;
        int   chunk_j    = 0;
        int   global_i   = 0;
        int   global_j   = 0;
        float brightness = 0.0f;
        float raw_intensity = 0.0f;
        SDL_FPoint direction{0.0f, 0.0f};
        SDL_Color color{255, 255, 255, 255};
        bool      has_direction = false;
=======
        int        chunk_i        = 0;
        int        chunk_j        = 0;
        int        global_i       = 0;
        int        global_j       = 0;
        float      brightness     = 0.0f;
        SDL_Color  color{255, 255, 255, 255};
        SDL_FPoint world_position{0.0f, 0.0f};
>>>>>>> theirs
    };

    std::vector<Sample> samples{};

    SDL_FPoint brightest_centroid{0.0f, 0.0f};
    SDL_FPoint brightest_direction{0.0f, 0.0f};
    SDL_FPoint brightest_sample_position{0.0f, 0.0f};
    float      brightest_sample_brightness = 0.0f;
    std::size_t brightest_sample_count     = 0;
    bool       has_brightest_centroid      = false;
    bool       has_brightest_direction     = false;
    bool       has_brightest_sample        = false;

    bool empty() const { return samples.empty(); }
};

struct ExternalLightSample {
    SDL_FPoint position{0.0f, 0.0f};
    float      radius    = 0.0f;
    float      intensity = 0.0f; // normalized [0,1]
    SDL_Color  color{255, 255, 255, 255};
    SDL_FPoint direction{0.0f, -1.0f};
    bool       has_direction = false;
    struct Attenuation {
        float constant  = 1.0f;
        float linear    = 0.0f;
        float quadratic = 0.0f;
        bool  enabled   = false;
    } attenuation{};
};

class RuntimeLightingSampler {
public:
    explicit RuntimeLightingSampler(Assets* assets);

    void set_assets(Assets* assets) { assets_ = assets; }
    void begin_frame();
    void add_external_sample(const ExternalLightSample& sample);

    RuntimeLightingFrame gather(const std::vector<AssetLight>& asset_lights,
                                const camera&                  cam);

private:
    Assets* assets_ = nullptr;
    std::vector<ExternalLightSample> external_samples_{};
};

} // namespace runtime_lighting

