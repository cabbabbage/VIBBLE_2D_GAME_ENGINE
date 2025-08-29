#include "asset_info.hpp"
#include "utils/cache_manager.hpp"
#include "utils/generate_light.hpp"
#include <SDL_image.h>
#include <iostream>
#include <fstream>
#include <random>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include "asset_info_methods/animation_loader.hpp"
#include "asset_info_methods/lighting_loader.hpp"
#include "asset_info_methods/area_loader.hpp"
#include "asset_info_methods/child_loader.hpp"

namespace fs = std::filesystem;

static SDL_BlendMode parse_blend_mode(const std::string& mode_str) {
    if (mode_str == "SDL_BLENDMODE_NONE") return SDL_BLENDMODE_NONE;
    if (mode_str == "SDL_BLENDMODE_BLEND") return SDL_BLENDMODE_BLEND;
    if (mode_str == "SDL_BLENDMODE_ADD")   return SDL_BLENDMODE_ADD;
    if (mode_str == "SDL_BLENDMODE_MOD")   return SDL_BLENDMODE_MOD;
    if (mode_str == "SDL_BLENDMODE_MUL")   return SDL_BLENDMODE_MUL;
    return SDL_BLENDMODE_BLEND;
}

AssetInfo::AssetInfo(const std::string& asset_folder_name)
    : blendmode(SDL_BLENDMODE_BLEND),
      has_light_source(false),
      has_shading(false),
      has_base_shadow(false),
      has_gradient_shadow(false),
      has_casted_shadows(false)
{
    name = asset_folder_name;
    dir_path_ = "SRC/" + asset_folder_name;
    std::string info_path = dir_path_ + "/info.json";
    info_json_path_ = info_path;

    std::ifstream in(info_path);
    if (!in.is_open()) {
        throw std::runtime_error("Failed to open asset info: " + info_path);
    }
    nlohmann::json data;
    in >> data;
    info_json_ = data; // keep a snapshot for updates

    
    if (data.contains("blend_mode") && data["blend_mode"].is_string()) {
        blendmode = parse_blend_mode(data["blend_mode"].get<std::string>());
    }

    
    tags.clear();
    if (data.contains("tags") && data["tags"].is_array()) {
        for (const auto& tag : data["tags"]) {
            if (tag.is_string()) {
                std::string str = tag.get<std::string>();
                if (!str.empty())
                    tags.push_back(str);
            }
        }
    }

    
    if (data.contains("animations")) {
        anims_json_ = data["animations"];
    }

    
    load_base_properties(data);
    
    
    LightingLoader::load(*this, data);
    
    
    const auto& ss = data.value("size_settings", nlohmann::json::object());
    scale_factor = ss.value("scale_percentage", 100.0f) / 100.0f;

    
    int scaled_canvas_w = static_cast<int>(original_canvas_width * scale_factor);
    int scaled_canvas_h = static_cast<int>(original_canvas_height * scale_factor);
    int offset_x = (scaled_canvas_w - 0) / 2;
    int offset_y = (scaled_canvas_h - 0);

    AreaLoader::load_collision_areas(*this, data, dir_path_, offset_x, offset_y);
    ChildLoader::load_children(*this, data, dir_path_);
}

AssetInfo::~AssetInfo() {
    std::ostringstream oss;
    oss << "[AssetInfo] Destructor for '" << name << "'\r";
    std::cout << std::left << std::setw(60) << oss.str() << std::flush;

    for (auto& [key, anim] : animations) {
        for (SDL_Texture* tex : anim.frames) {
            if (tex) SDL_DestroyTexture(tex);
        }
        anim.frames.clear();
    }
    animations.clear();
}

void AssetInfo::loadAnimations(SDL_Renderer* renderer) {
    if (anims_json_.is_null()) return;

    SDL_Texture* base_sprite = nullptr;
    int scaled_sprite_w = 0;
    int scaled_sprite_h = 0;
    generate_lights(renderer);

    CacheManager cache;
    std::string root_cache = "cache/" + name + "/animations";

    for (auto it = anims_json_.begin(); it != anims_json_.end(); ++it) {
        const std::string& trigger = it.key();
        const auto& anim_json = it.value();
        if (anim_json.is_null() || !anim_json.contains("frames_path"))
            continue;

        Animation anim;
        anim.load(trigger,
                  anim_json,
                  dir_path_,
                  root_cache,
                  scale_factor,
                  blendmode,
                  renderer,
                  base_sprite,
                  scaled_sprite_w,
                  scaled_sprite_h,
                  original_canvas_width,
                  original_canvas_height);

        if (!anim.frames.empty()) {
            animations[trigger] = std::move(anim);
        }
    }

    get_area_textures(renderer);
}

void AssetInfo::get_area_textures(SDL_Renderer* renderer) {
    AnimationLoader::get_area_textures(*this, renderer);
}

void AssetInfo::load_base_properties(const nlohmann::json& data) {
    type = data.value("asset_type", "Object");
    if (type == "Player") {
        std::cout << "[AssetInfo] Player asset '" << name << "' loaded\n\n";
    }

    z_threshold = data.value("z_threshold", 0);
    passable = has_tag("passable");

    min_same_type_distance = data.value("min_same_type_distance", 0);
    min_distance_all = data.value("min_distance_all", 0);
    has_shading = data.value("has_shading", false);
    flipable = data.value("can_invert", false);
}

void AssetInfo::load_lighting_info(const nlohmann::json& data) { LightingLoader::load(*this, data); }


void AssetInfo::load_collision_areas(const nlohmann::json& data,
                                     const std::string& dir_path,
                                     int offset_x,
                                     int offset_y) {
    AreaLoader::load_collision_areas(*this, data, dir_path, offset_x, offset_y);
}

void AssetInfo::load_child_json_paths(const nlohmann::json& data,
                                      const std::string& dir_path) {
    ChildLoader::load_children(*this, data, dir_path);
}

void AssetInfo::load_animations(const nlohmann::json& anims_json,
                                const std::string& dir_path,
                                SDL_Renderer* renderer,
                                SDL_Texture*& base_sprite,
                                int& scaled_sprite_w,
                                int& scaled_sprite_h)
{
    CacheManager cache;
    std::string root_cache = "cache/" + name + "/animations";

    for (auto it = anims_json.begin(); it != anims_json.end(); ++it) {
        const std::string& trigger = it.key();
        const auto& anim_json = it.value();
        if (anim_json.is_null() || !anim_json.contains("frames_path"))
            continue;

        std::string src_folder   = dir_path + "/" + anim_json["frames_path"].get<std::string>();
        std::string cache_folder = root_cache + "/" + trigger;
        std::string meta_file    = cache_folder + "/metadata.json";

        int expected_frames = 0;
        int orig_w = 0, orig_h = 0;
        while (true) {
            std::string f = src_folder + "/" + std::to_string(expected_frames) + ".png";
            if (!std::filesystem::exists(f)) break;
            if (expected_frames == 0) {
                if (SDL_Surface* s = IMG_Load(f.c_str())) {
                    orig_w = s->w;
                    orig_h = s->h;
                    SDL_FreeSurface(s);
                }
            }
            ++expected_frames;
        }
        if (expected_frames == 0) continue;

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
                    std::cerr << "[AssetInfo] Failed to load or scale: " << f << "\n";
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

        Animation anim;
        anim.on_end    = anim_json.value("on_end", "");
        anim.randomize = anim_json.value("randomize", false);
        anim.loop      = (anim.on_end == trigger);

        for (SDL_Surface* surf : surfaces) {
            SDL_Texture* tex = cache.surface_to_texture(renderer, surf);
            SDL_FreeSurface(surf);
            if (!tex) {
                std::cerr << "[AssetInfo] Failed to create texture for '" << trigger << "'\n";
                continue;
            }
            SDL_SetTextureBlendMode(tex, blendmode);
            anim.frames.push_back(tex);
        }

        if (trigger == "default" && !anim.frames.empty()) {
            base_sprite = anim.frames[0];
        }

        animations[trigger] = std::move(anim);
    }
}

void AssetInfo::try_load_area(const nlohmann::json& data,
                              const std::string& key,
                              const std::string& dir,
                              std::unique_ptr<Area>& area_ref,
                              bool& flag_ref,
                              float scale,
                              int offset_x,
                              int offset_y)
{
    bool area_loaded = false;

    if (data.contains(key) && data[key].is_string()) {
        try {
            std::string filename = data[key].get<std::string>();
            std::string path = dir + "/" + filename;
            std::string name = fs::path(filename).stem().string();

            area_ref = std::make_unique<Area>(name, path, scale);
            area_ref->apply_offset(offset_x, offset_y);
            flag_ref = true;
            area_loaded = true;
        } catch (const std::exception& e) {
            std::cerr << "[AssetInfo] warning: failed to load area '"
                      << key << "': " << e.what() << std::endl;
        }
    }

    if (!area_loaded && key == "spacing_area") {
        std::string fallback_name = name + "_circle_spacing";
        int radius = static_cast<int>(std::ceil(std::max(original_canvas_width, original_canvas_height) * scale / 2.0f));
        int center_x = offset_x;
        int center_y = offset_y;
        int size = radius * 2;

        area_ref = std::make_unique<Area>(
            fallback_name,
            center_x,
            center_y,
            size,
            size,
            "Circle",
            1,
            std::numeric_limits<int>::max(),
            std::numeric_limits<int>::max()
        );
        flag_ref = true;
        std::cerr << "[AssetInfo] fallback: created circular spacing area for '" << name << "'\n";
    }
}

bool AssetInfo::has_tag(const std::string& tag) const {
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

void AssetInfo::generate_lights(SDL_Renderer* renderer) { LightingLoader::generate_textures(*this, renderer); }

// --------------------- Update API ---------------------

static std::string blend_mode_to_string(SDL_BlendMode mode) {
    switch (mode) {
        case SDL_BLENDMODE_NONE: return "SDL_BLENDMODE_NONE";
        case SDL_BLENDMODE_BLEND: return "SDL_BLENDMODE_BLEND";
        case SDL_BLENDMODE_ADD: return "SDL_BLENDMODE_ADD";
        case SDL_BLENDMODE_MOD: return "SDL_BLENDMODE_MOD";
        case SDL_BLENDMODE_MUL: return "SDL_BLENDMODE_MUL";
        default: return "SDL_BLENDMODE_BLEND";
    }
}

bool AssetInfo::update_info_json() const {
    try {
        std::ofstream out(info_json_path_);
        if (!out.is_open()) return false;
        out << info_json_.dump(4);
        return true;
    } catch (...) {
        return false;
    }
}

void AssetInfo::set_asset_type(const std::string& t) {
    type = t;
    info_json_["asset_type"] = t;
}

void AssetInfo::set_z_threshold(int z) {
    z_threshold = z;
    info_json_["z_threshold"] = z;
}

void AssetInfo::set_min_same_type_distance(int d) {
    min_same_type_distance = d;
    info_json_["min_same_type_distance"] = d;
}

void AssetInfo::set_min_distance_all(int d) {
    min_distance_all = d;
    info_json_["min_distance_all"] = d;
}

void AssetInfo::set_has_shading(bool v) {
    has_shading = v;
    info_json_["has_shading"] = v;
}

void AssetInfo::set_flipable(bool v) {
    flipable = v;
    info_json_["can_invert"] = v;
}

void AssetInfo::set_blend_mode(SDL_BlendMode mode) {
    blendmode = mode;
    info_json_["blend_mode"] = blend_mode_to_string(mode);
}

void AssetInfo::set_blend_mode_string(const std::string& mode_str) {
    info_json_["blend_mode"] = mode_str;
    blendmode = parse_blend_mode(mode_str);
}

void AssetInfo::set_scale_factor(float factor) {
    if (factor < 0.f) factor = 0.f;
    scale_factor = factor;
    // Ensure size_settings exists
    if (!info_json_.contains("size_settings") || !info_json_["size_settings"].is_object()) {
        info_json_["size_settings"] = nlohmann::json::object();
    }
    info_json_["size_settings"]["scale_percentage"] = factor * 100.0f;
}

void AssetInfo::set_scale_percentage(float percent) {
    scale_factor = percent / 100.0f;
    if (!info_json_.contains("size_settings") || !info_json_["size_settings"].is_object()) {
        info_json_["size_settings"] = nlohmann::json::object();
    }
    info_json_["size_settings"]["scale_percentage"] = percent;
}

void AssetInfo::set_tags(const std::vector<std::string>& t) {
    tags = t;
    // reflect in JSON
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& s : tags) arr.push_back(s);
    info_json_["tags"] = std::move(arr);
    // update passable cache
    passable = has_tag("passable");
}

void AssetInfo::add_tag(const std::string& tag) {
    if (std::find(tags.begin(), tags.end(), tag) == tags.end()) {
        tags.push_back(tag);
    }
    set_tags(tags); // syncs json + passable
}

void AssetInfo::remove_tag(const std::string& tag) {
    tags.erase(std::remove(tags.begin(), tags.end(), tag), tags.end());
    set_tags(tags); // syncs json + passable
}

void AssetInfo::set_passable(bool v) {
    passable = v;
    if (v) add_tag("passable");
    else   remove_tag("passable");
}
