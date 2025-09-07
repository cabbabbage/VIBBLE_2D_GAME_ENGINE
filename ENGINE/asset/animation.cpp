#include "animation.hpp"
#include "asset/asset_info.hpp"
#include "utils/cache_manager.hpp"
#include <SDL_image.h>
#include <algorithm>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

Animation::Animation() = default;

void Animation::load(const std::string& trigger,
                     const nlohmann::json& anim_json,
                     AssetInfo& info,
                     const std::string& dir_path,
                     const std::string& root_cache,
                     float scale_factor,
                     SDL_Renderer* renderer,
                     SDL_Texture*& base_sprite,
                     int& scaled_sprite_w,
                     int& scaled_sprite_h,
                     int& original_canvas_width,
                     int& original_canvas_height)
{
    CacheManager cache;

    if (anim_json.contains("source")) {
        const auto& s = anim_json["source"];
        try {
            if (s.contains("kind") && s["kind"].is_string())
                source.kind = s["kind"].get<std::string>();
            else
                source.kind = "folder";
        } catch (...) { source.kind = "folder"; }
        try {
            if (s.contains("path") && s["path"].is_string())
                source.path = s["path"].get<std::string>();
            else
                source.path.clear();
        } catch (...) { source.path.clear(); }
        try {
            if (s.contains("name") && s["name"].is_string())
                source.name = s["name"].get<std::string>();
            else
                source.name.clear();
        } catch (...) { source.name.clear(); }
    }
    flipped_source = anim_json.value("flipped_source", false);
    reverse_source = anim_json.value("reverse_source", false);
    locked         = anim_json.value("locked", false);
    // Speed handling:
    //  - Positive values behave as usual (frames advance by speed_factor per tick)
    //  - Negative values mean "slow down by that factor"; e.g. -2 -> advance by 1/2 per tick
    //  - Zero keeps animation paused (no frame advance)
    speed_factor   = anim_json.value("speed_factor", 1.0f);
    if (speed_factor < 0.0f) {
        float mag = -speed_factor; // slowdown factor magnitude
        // Prevent divide-by-zero or denormals
        if (mag < 0.0001f) mag = 0.0001f;
        speed_factor = 1.0f / mag;
    }
    loop           = anim_json.value("loop", true);
    randomize      = anim_json.value("randomize", false);
    // New: optional random-start flag (alias/alternative to randomize)
    rnd_start      = anim_json.value("rnd_start", false);
    on_end_mapping = anim_json.value("on_end_mapping", "");
    on_end_animation = anim_json.value("on_end", std::string{});

    // Parse movement frames: [dx, dy, opt_bool_sort_x_index, opt_rgb[3]]
    total_dx = 0;
    total_dy = 0;
    movement.clear();
    if (anim_json.contains("movement") && anim_json["movement"].is_array()) {
        for (const auto& mv : anim_json["movement"]) {
            if (!mv.is_array() || mv.size() < 2) continue;
            FrameMovement fm;
            try { fm.dx = mv[0].get<int>(); } catch (...) { fm.dx = 0; }
            try { fm.dy = mv[1].get<int>(); } catch (...) { fm.dy = 0; }
            if (mv.size() >= 3 && mv[2].is_boolean()) {
                fm.sort_z_index = mv[2].get<bool>();
            }
            if (mv.size() >= 4 && mv[3].is_array() && mv[3].size() >= 3) {
                auto clamp = [](int v) { return (v < 0) ? 0 : (v > 255 ? 255 : v); };
                int r = 255, g = 255, b = 255;
                try { r = clamp(mv[3][0].get<int>()); } catch (...) { r = 255; }
                try { g = clamp(mv[3][1].get<int>()); } catch (...) { g = 255; }
                try { b = clamp(mv[3][2].get<int>()); } catch (...) { b = 255; }
                fm.rgb = SDL_Color{ static_cast<Uint8>(r), static_cast<Uint8>(g), static_cast<Uint8>(b), 255 };
            }
            movement.push_back(fm);
            total_dx += fm.dx;
            total_dy += fm.dy;
        }
    }
    // Movement flag: true only if totals are not both zero
    movment = !(total_dx == 0 && total_dy == 0);

    // Load frames depending on source kind
    if (source.kind == "animation" && !source.name.empty()) {
        auto it = info.animations.find(source.name);
        if (it != info.animations.end()) {
            // Duplicate frames from the referenced animation, optionally flipped
            const auto& src_frames = it->second.frames;
            for (SDL_Texture* src : src_frames) {
                if (!src) continue;
                Uint32 fmt; int access, w, h;
                if (SDL_QueryTexture(src, &fmt, &access, &w, &h) != 0) {
                    continue;
                }
                SDL_Texture* dst = SDL_CreateTexture(renderer, fmt, SDL_TEXTUREACCESS_TARGET, w, h);
                if (!dst) continue;
                SDL_SetTextureBlendMode(dst, SDL_BLENDMODE_BLEND);
                SDL_SetRenderTarget(renderer, dst);
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
                SDL_RenderClear(renderer);
                SDL_Rect r{0, 0, w, h};
                SDL_RendererFlip flip = flipped_source ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
                SDL_RenderCopyEx(renderer, src, nullptr, &r, 0.0, nullptr, flip);
                SDL_SetRenderTarget(renderer, nullptr);
                frames.push_back(dst);
            }
            if (reverse_source) {
                std::reverse(frames.begin(), frames.end());
            }
        }
    } else {
        std::string src_folder   = dir_path + "/" + source.path;
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
                meta.value("original_height", -1) == orig_h)
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
            cache.save_metadata(meta_file, new_meta);
        }

        for (SDL_Surface* surf : surfaces) {
            SDL_Texture* tex = cache.surface_to_texture(renderer, surf);
            SDL_FreeSurface(surf);
            if (!tex) {
                std::cerr << "[Animation] Failed to create texture for '" << trigger << "'\n";
                continue;
            }
            frames.push_back(tex);
        }

        // Apply optional flip for folder sources after textures are made
        if (flipped_source && !frames.empty()) {
            std::vector<SDL_Texture*> flipped;
            flipped.reserve(frames.size());
            for (SDL_Texture* src : frames) {
                if (!src) { flipped.push_back(nullptr); continue; }
                Uint32 fmt; int access, w, h;
                if (SDL_QueryTexture(src, &fmt, &access, &w, &h) != 0) {
                    flipped.push_back(nullptr);
                    continue;
                }
                SDL_Texture* dst = SDL_CreateTexture(renderer, fmt, SDL_TEXTUREACCESS_TARGET, w, h);
                if (!dst) { flipped.push_back(nullptr); continue; }
                SDL_SetTextureBlendMode(dst, SDL_BLENDMODE_BLEND);
                SDL_SetRenderTarget(renderer, dst);
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
                SDL_RenderClear(renderer);
                SDL_Rect r{0, 0, w, h};
                SDL_RenderCopyEx(renderer, src, nullptr, &r, 0.0, nullptr, SDL_FLIP_HORIZONTAL);
                SDL_SetRenderTarget(renderer, nullptr);
                flipped.push_back(dst);
            }
            // Destroy original unflipped textures before replacing
            for (SDL_Texture* t : frames) {
                if (t) SDL_DestroyTexture(t);
            }
            frames.swap(flipped);
        }

        // Optional reverse ordering for folder sources
        if (reverse_source && !frames.empty()) {
            std::reverse(frames.begin(), frames.end());
        }
    }

    number_of_frames = static_cast<int>(frames.size());

    if (trigger == "default" && !frames.empty()) {
        base_sprite = frames[0];
    }
}

SDL_Texture* Animation::get_frame(int index) const {
    if (index < 0 || index >= static_cast<int>(frames.size())) return nullptr;
    return frames[index];
}

bool Animation::advance(int& index,
                        float& progress,
                        int& dx,
                        int& dy,
                        std::string& mapping_id,
                        bool& resort_z) const
{
    if (frozen || frames.empty()) return false;

    // Movement should sync with frame progression. Apply movement only when
    // a frame boundary is crossed, and accumulate for multi-frame jumps.
    dx = 0;
    dy = 0;
    resort_z = false;

    progress += speed_factor;

    // Track whether we hit the end of a non-looping animation
    bool reached_end = false;

    while (progress >= 1.0f) {
        progress -= 1.0f;
        ++index;

        if (index < number_of_frames) {
            if (index < static_cast<int>(movement.size())) {
                dx += movement[index].dx;
                dy += movement[index].dy;
                resort_z = resort_z || movement[index].sort_z_index;
            }
            continue;
        }

        // We stepped past the last frame; handle loop or finalize
        if (loop && number_of_frames > 0) {
            index = 0;
            if (!movement.empty()) {
                dx += movement[0].dx;
                dy += movement[0].dy;
                resort_z = resort_z || movement[0].sort_z_index;
            }
            // keep looping if progress still >= 1
        } else {
            // Non-looping: finalize and signal mapping/next
            reached_end = true;
            index = number_of_frames > 0 ? number_of_frames - 1 : 0;
            break;
        }
    }

    if (!reached_end) {
        // We're still within the animation (or looped). Return true to indicate
        // the animation is active. If speed is slow and no frame boundary was
        // crossed, dx/dy will remain zero, preserving sync with frames.
        return true;
    }

    // End reached on a non-looping animation; set mapping if specified.
    if (!on_end_mapping.empty()) {
        mapping_id = on_end_mapping; // treated as mapping id
        return false;
    }

    if (!on_end_animation.empty()) {
        mapping_id = on_end_animation; // treated as direct animation id
        return false;
    }

    return false;
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
