#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <SDL.h>
#include <nlohmann/json.hpp>
#include "animation_child_data.hpp"
#include "animation_frame.hpp"
#include "render/render.hpp"
#include "animation_frame_variant.hpp"

inline constexpr int kBaseAnimationFps = 24;

class AssetInfo;
struct Mix_Chunk;

class Animation {

    friend class AnimationLoader;
    friend class AnimationCloner;
public:
    enum class OnEndDirective {
        Default,
        Kill,
        Lock,
        Reverse,
        Animation,
};

    struct FrameCache {
        std::vector<SDL_Texture*> textures;
        std::vector<int> widths;
        std::vector<int> heights;
        std::vector<SDL_Texture*> foreground_textures;
        std::vector<SDL_Texture*> background_textures;
        std::vector<SDL_Texture*> mask_textures;
        std::vector<int> mask_widths;
        std::vector<int> mask_heights;

        void resize(std::size_t variant_count) {
            textures.assign(variant_count, nullptr);
            widths.assign(variant_count, 0);
            heights.assign(variant_count, 0);
            foreground_textures.assign(variant_count, nullptr);
            background_textures.assign(variant_count, nullptr);
            mask_textures.assign(variant_count, nullptr);
            mask_widths.assign(variant_count, 0);
            mask_heights.assign(variant_count, 0);
        }
};

    struct AudioClip {
        std::string name;
        std::string path;
        int volume = 100;
        bool effects = false;
        std::shared_ptr<Mix_Chunk> chunk;
};

    Animation();
    const FrameVariant* get_frame(const AnimationFrame* frame, float requested_scale) const;
    const AnimationFrame* get_first_frame(std::size_t path_index = 0) const;
    AnimationFrame* get_first_frame(std::size_t path_index = 0);
    int index_of(const AnimationFrame* frame) const;
    void change(AnimationFrame*& frame, bool& static_flag) const;
    void freeze();
    bool is_frozen() const;
    bool has_audio() const;
    Mix_Chunk* audio_chunk() const;
    const AudioClip* audio_data() const;
    void clear_texture_cache();
    void adopt_prebuilt_frames(std::vector<FrameCache> caches, std::vector<SDL_Texture*> base_frames, std::vector<SDL_Texture*> base_masks, std::vector<float> variant_steps);

    bool rebuild_frame(int frame_index, SDL_Renderer* renderer, const AssetInfo& info, const std::string& animation_id);

    bool rebuild_animation(SDL_Renderer* renderer, const AssetInfo& info, const std::string& animation_id);
    bool copy_from(const Animation& source, bool flip_horizontal, bool flip_vertical, bool reverse_frames, SDL_Renderer* renderer, class AssetInfo& info);
    static OnEndDirective classify_on_end(std::string_view value);
    struct Source {
        std::string kind;
        std::string path;
        std::string name;
    } source{};
    bool flipped_source = false;
    bool flip_vertical_source = false;
    bool flip_movement_horizontal = false;
    bool flip_movement_vertical = false;
    bool reverse_source = false;
    bool inherit_source_movement = false;
    bool locked = false;
    int number_of_frames = 0;
    int total_dx = 0;
    int total_dy = 0;
    bool movment = false;
    bool rnd_start = false;
    std::string on_end_animation;
    OnEndDirective on_end_behavior = OnEndDirective::Default;
    std::vector<AnimationFrame*> frames;
    bool randomize = false;
    bool loop = true;
    bool frozen = false;
    SDL_Texture* preview_texture = nullptr;
    std::size_t movement_path_count() const;
    const std::vector<AnimationFrame>& movement_path(std::size_t index) const;
    std::vector<AnimationFrame>& movement_path(std::size_t index);
    void inherit_movement_from(const Animation& source);
    std::size_t default_movement_path_index() const { return 0; }
    std::size_t clamp_path_index(std::size_t index) const;
    std::size_t variant_count() const { return variant_steps_.size(); }
    const std::vector<float>& variant_steps() const { return variant_steps_; }
    const std::vector<std::string>& child_assets() const { return child_asset_names_; }
    std::vector<std::string>& child_assets() { return child_asset_names_; }
    bool has_child_assets() const { return !child_asset_names_.empty(); }
    const std::vector<AnimationChildData>& child_timelines() const { return child_data_; }
    std::vector<AnimationChildData>& child_timelines() { return child_data_; }
    const AnimationChildData* find_child_timeline(std::string_view child_name) const;
    AnimationChildData* find_child_timeline(std::string_view child_name);
    void rebuild_child_timelines_from_frames();
    void refresh_child_start_events() { rebuild_child_start_events_from_timelines(); }
private:
    std::vector<FrameCache> frame_cache_;
    AudioClip audio_clip;
    std::vector<std::vector<AnimationFrame>> movement_paths_;
    std::vector<float> variant_steps_;
    std::vector<std::string> child_asset_names_;
    std::vector<AnimationChildData> child_data_;
    void rebuild_child_start_events_from_timelines();
    void bind_textures_to_frame(AnimationFrame& frame) const;
    void refresh_frame_texture_bindings();
};
