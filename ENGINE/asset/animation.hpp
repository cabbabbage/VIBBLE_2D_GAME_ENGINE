#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <SDL.h>
#include <nlohmann/json.hpp>
#include "animation_frame.hpp"
#include "render_pipeline/ScalingLogic.hpp"

class AssetInfo;
struct Mix_Chunk;

class Animation {

public:
    struct AudioClip {
        std::string name;
        std::string path;
        int volume = 100;
        bool effects = false;
        std::shared_ptr<Mix_Chunk> chunk;
};

public:
    Animation();
    void load(const std::string& trigger, const nlohmann::json& anim_json, class AssetInfo& info, const std::string& dir_path, const std::string& root_cache, float scale_factor, SDL_Renderer* renderer, SDL_Texture*& base_sprite, int& scaled_sprite_w, int& scaled_sprite_h, int& original_canvas_width, int& original_canvas_height, bool scaling_refresh_pending);
    SDL_Texture* get_frame(const AnimationFrame* frame) const;
    AnimationFrame* get_first_frame(std::size_t path_index = 0);
    int index_of(const AnimationFrame* frame) const;
    void change(AnimationFrame*& frame, bool& static_flag) const;
    void freeze();
    bool is_frozen() const;
   bool is_static() const;
   bool has_audio() const;
   Mix_Chunk* audio_chunk() const;
    const AudioClip* audio_data() const;
    void clear_texture_cache();
    SDL_Texture* frame_variant(std::size_t frame_index, std::size_t variant_index) const;
    SDL_Texture* mask_variant(std::size_t frame_index, std::size_t variant_index) const;
    void adopt_prebuilt_frames(std::vector<FrameCache> caches,
                               std::vector<SDL_Texture*> base_frames,
                               std::vector<SDL_Texture*> base_masks,
                               std::vector<float> variant_steps);

    struct FrameCache {
        std::vector<SDL_Texture*> textures;
        std::vector<int> widths;
        std::vector<int> heights;
        std::vector<SDL_Texture*> mask_textures;
        std::vector<int> mask_widths;
        std::vector<int> mask_heights;

        void resize(std::size_t variant_count) {
            textures.assign(variant_count, nullptr);
            widths.assign(variant_count, 0);
            heights.assign(variant_count, 0);
            mask_textures.assign(variant_count, nullptr);
            mask_widths.assign(variant_count, 0);
            mask_heights.assign(variant_count, 0);
        }
    };
    struct Source {
    std::string kind;
    std::string path;
    std::string name;
    } source{};
    bool flipped_source = false;
    bool reverse_source = false;
    bool locked = false;
    float speed_factor = 1.0f;
    int number_of_frames = 0;
    int total_dx = 0;
    int total_dy = 0;
    bool movment = false;
    bool rnd_start = false;
    std::string on_end_mapping;
    std::string on_end_animation;
    std::vector<SDL_Texture*> frames;
    std::vector<SDL_Texture*> mask_frames;
    bool randomize = false;
    bool loop = true;
    bool frozen = false;
    std::size_t movement_path_count() const;
    const std::vector<AnimationFrame>& movement_path(std::size_t index) const;
    std::vector<AnimationFrame>& movement_path(std::size_t index);
    std::size_t default_movement_path_index() const { return 0; }
    std::size_t clamp_path_index(std::size_t index) const;
    std::size_t variant_count() const { return variant_steps_.size(); }
    const std::vector<float>& variant_steps() const { return variant_steps_; }
private:
    std::vector<FrameCache> frame_cache_;
    AudioClip audio_clip;
    std::vector<std::vector<AnimationFrame>> movement_paths_;
    std::vector<float> variant_steps_;
};
