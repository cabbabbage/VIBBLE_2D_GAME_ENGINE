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
    speed_factor   = anim_json.value("speed_factor", 1.0f);
    loop           = anim_json.value("loop", true);
    randomize      = anim_json.value("randomize", false);
    on_end_mapping = anim_json.value("on_end_mapping", "");
    on_end_animation = anim_json.value("on_end", std::string{});

    if (anim_json.contains("movement") && anim_json["movement"].is_array()) {
        for (const auto& mv : anim_json["movement"]) {
            if (mv.is_array() && mv.size() >= 2) {
                movement.emplace_back(mv[0].get<int>(), mv[1].get<int>());
            }
        }
    }

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
                        std::string& mapping_id) const
{
    if (frozen || frames.empty()) return false;

    progress += speed_factor;
    bool frame_changed = false;
    while (progress >= 1.0f) {
        progress -= 1.0f;
        ++index;
        frame_changed = true;
    }

    if (index < number_of_frames) {
        if (index < static_cast<int>(movement.size())) {
            dx = movement[index].first;
            dy = movement[index].second;
        }
        return true;
    }

    if (loop) {
        index = 0;
        if (!movement.empty()) {
            dx = movement[0].first;
            dy = movement[0].second;
        }
        return true;
    }

    if (!on_end_mapping.empty()) {
        mapping_id = on_end_mapping; // treated as mapping id
        index = number_of_frames > 0 ? number_of_frames - 1 : 0;
        return false;
    }

    if (!on_end_animation.empty()) {
        mapping_id = on_end_animation; // treated as direct animation id
        index = number_of_frames > 0 ? number_of_frames - 1 : 0;
        return false;
    }

    index = number_of_frames > 0 ? number_of_frames - 1 : 0;
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

