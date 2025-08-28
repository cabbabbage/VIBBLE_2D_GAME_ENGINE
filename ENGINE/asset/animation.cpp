

#include "animation.hpp"
#include "utils/cache_manager.hpp"
#include <SDL_image.h>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

Animation::Animation() = default;

void Animation::load(const std::string& trigger,
                     const nlohmann::json& anim_json,
                     const std::string& dir_path,
                     const std::string& root_cache,
                     float scale_factor,
                     SDL_BlendMode blendmode,
                     SDL_Renderer* renderer,
                     SDL_Texture*& base_sprite,
                     int& scaled_sprite_w,
                     int& scaled_sprite_h,
                     int& original_canvas_width,
                     int& original_canvas_height)
{
    CacheManager cache;
    std::string src_folder   = dir_path + "/" + anim_json["frames_path"].get<std::string>();
    std::string cache_folder = root_cache + "/" + trigger;
    std::string meta_file    = cache_folder + "/metadata.json";

    int expected_frames = 0;
    int orig_w = 0, orig_h = 0;
    while (true) {
        std::string f = src_folder + "/" + std::to_string(expected_frames) + ".png";
        if (!fs::exists(f)) break;
        if (expected_frames == 0) {
            if (SDL_Surface* s = IMG_Load(f.c_str())) {
                orig_w = s->w;
                orig_h = s->h;
                SDL_FreeSurface(s);
            }
        }
        ++expected_frames;
    }
    if (expected_frames == 0) return;

    bool use_cache = false;
    nlohmann::json meta;
    if (cache.load_metadata(meta_file, meta)) {
        if (meta.value("frame_count", -1) == expected_frames &&
            meta.value("scale_factor", -1.0f) == scale_factor &&
            meta.value("original_width", -1) == orig_w &&
            meta.value("original_height", -1) == orig_h &&
            meta.value("blend_mode", -1) == int(blendmode))
        {
            use_cache = true;
        }
    }

    std::vector<SDL_Surface*> surfaces;
    if (use_cache) {
        use_cache = cache.load_surface_sequence(cache_folder, expected_frames, surfaces);
    }

    if (!use_cache) {
        surfaces.clear();
        for (int i = 0; i < expected_frames; ++i) {
            std::string f = src_folder + "/" + std::to_string(i) + ".png";
            int new_w = 0, new_h = 0;
            SDL_Surface* scaled = cache.load_and_scale_surface(f, scale_factor, new_w, new_h);
            if (!scaled) {
                std::cerr << "[Animation] Failed to load or scale: " << f << "\n";
                continue;
            }
            SDL_SetSurfaceBlendMode(scaled, blendmode);
            if (i == 0) {
                original_canvas_width  = orig_w;
                original_canvas_height = orig_h;
                scaled_sprite_w = new_w;
                scaled_sprite_h = new_h;
            }
            surfaces.push_back(scaled);
        }
        cache.save_surface_sequence(cache_folder, surfaces);

        nlohmann::json new_meta;
        new_meta["frame_count"]     = expected_frames;
        new_meta["scale_factor"]    = scale_factor;
        new_meta["original_width"]  = orig_w;
        new_meta["original_height"] = orig_h;
        new_meta["blend_mode"]      = int(blendmode);
        cache.save_metadata(meta_file, new_meta);
    }

    on_end           = anim_json.value("on_end", "");
    randomize        = anim_json.value("randomize", false);
    
    loop             = anim_json.value("loop", true);
    lock_until_done  = anim_json.value("lock_until_done", false);

    for (SDL_Surface* surf : surfaces) {
        SDL_Texture* tex = cache.surface_to_texture(renderer, surf);
        SDL_FreeSurface(surf);
        if (!tex) {
            std::cerr << "[Animation] Failed to create texture for '" << trigger << "'\n";
            continue;
        }
        SDL_SetTextureBlendMode(tex, blendmode);
        frames.push_back(tex);
    }

    if (trigger == "default" && !frames.empty()) {
        base_sprite = frames[0];
    }
}

SDL_Texture* Animation::get_frame(int index) const {
    if (index < 0 || index >= static_cast<int>(frames.size())) return nullptr;
    return frames[index];
}

bool Animation::advance(int& index, std::string& next_animation_name) const {
    if (frozen || frames.empty()) return false;

    ++index;
    if (index < static_cast<int>(frames.size())) {
        return true;
    }

    if (loop) {
        index = 0;
        return true;
    } else if (!on_end.empty()) {
        next_animation_name = on_end;
        return false;
    } else {
        index = static_cast<int>(frames.size()) - 1;
        return true;
    }
}

void Animation::change(int& index, bool& static_flag) const {
    if (frozen) return;
    index = 0;
    static_flag = is_static();
}

void Animation::freeze() {
    frozen = true;
}

bool Animation::is_frozen() const {
    return frozen;
}

bool Animation::is_static() const {
    return frames.size() <= 1;
}
