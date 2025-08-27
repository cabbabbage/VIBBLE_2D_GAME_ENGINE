#include "asset_info.hpp"
#include "utils/cache_manager.hpp"
#include <SDL_image.h>
#include <iostream>
#include <fstream>
#include <random>
#include <sstream>
#include <iomanip>

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

    std::ifstream in(info_path);
    if (!in.is_open()) {
        throw std::runtime_error("Failed to open asset info: " + info_path);
    }
    nlohmann::json data;
    in >> data;

    
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
        interaction = anims_json_.contains("interaction") && !anims_json_["interaction"].is_null();
        hit         = anims_json_.contains("hit")        && !anims_json_["hit"].is_null();
        collision   = anims_json_.contains("collision")  && !anims_json_["collision"].is_null();
    }

    
    load_base_properties(data);

    
    load_lighting_info(data);

    
    const auto& ss = data.value("size_settings", nlohmann::json::object());
    scale_percentage       = ss.value("scale_percentage", 100.0f);
    variability_percentage = ss.value("variability_percentage", 0.0f);
    scale_factor           = scale_percentage / 100.0f;

    
    int scaled_canvas_w = static_cast<int>(original_canvas_width * scale_factor);
    int scaled_canvas_h = static_cast<int>(original_canvas_height * scale_factor);
    int offset_x = (scaled_canvas_w - 0) / 2;
    int offset_y = (scaled_canvas_h - 0);

    load_collision_areas(data, dir_path_, offset_x, offset_y);
    load_child_json_paths(data, dir_path_);
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
    child_json_paths.clear();
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
    if (!renderer) return;

    CacheManager cache;

    auto try_load_or_create = [&](std::unique_ptr<Area>& area, const std::string& kind) {
        if (!area) return;

        std::string folder = "cache/areas/" + name + "_" + kind;
        std::string meta_file = folder + "/metadata.json";
        std::string bmp_file = folder + "/0.bmp";

        auto [minx, miny, maxx, maxy] = area->get_bounds();
        nlohmann::json meta;
        if (cache.load_metadata(meta_file, meta)) {
            if (meta.value("bounds", std::vector<int>{}) == std::vector<int>{minx, miny, maxx, maxy}) {
                SDL_Surface* surf = cache.load_surface(bmp_file);
                if (surf) {
                    SDL_Texture* tex = cache.surface_to_texture(renderer, surf);
                    SDL_FreeSurface(surf);
                    if (tex) {
                        
                        area->create_area_texture(renderer); 
                        return;
                    }
                }
            }
        }

        area->create_area_texture(renderer);

        
        area->create_area_texture(renderer);
        SDL_Texture* tex = area->get_texture();
        if (tex) {
            SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, maxx - minx + 1, maxy - miny + 1, 32, SDL_PIXELFORMAT_RGBA8888);
            if (surf) {
                SDL_SetRenderTarget(renderer, tex);
                SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGBA8888, surf->pixels, surf->pitch);
                cache.save_surface_as_png(surf, bmp_file);
                SDL_FreeSurface(surf);
                meta["bounds"] = {minx, miny, maxx, maxy};
                cache.save_metadata(meta_file, meta);
                SDL_SetRenderTarget(renderer, nullptr);
            }
        }
    };

    try_load_or_create(passability_area, "passability");
    try_load_or_create(spacing_area, "spacing");
    try_load_or_create(collision_area, "collision");
    try_load_or_create(interaction_area, "interaction");
    try_load_or_create(attack_area, "attack");
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
    can_invert = data.value("can_invert", true);
    max_child_depth = data.value("max_child_depth", 0);
    min_child_depth = data.value("min_child_depth", 0);
    duplicatable = data.value("duplicatable", false);
    duplication_interval_min = data.value("duplication_interval_min", 0);
    duplication_interval_max = data.value("duplication_interval_max", 0);
    update_radius = data.value("update_radius", 1000);
    has_shading = data.value("has_shading", false);
    flipable               = data.value("can_invert", false);

    std::mt19937 rng{ std::random_device{}() };
    if (min_child_depth <= max_child_depth) {
        child_depth = std::uniform_int_distribution<int>(min_child_depth, max_child_depth)(rng);
    } else {
        child_depth = 0;
    }




    if (duplication_interval_min <= duplication_interval_max && duplicatable) {
        duplication_interval = std::uniform_int_distribution<int>(duplication_interval_min, duplication_interval_max)(rng);
    } else {
        duplication_interval = 0;
    }
}

void AssetInfo::load_lighting_info(const nlohmann::json& data) {
    has_light_source = false;
    light_sources.clear();
    orbital_light_sources.clear();

    if (!data.contains("lighting_info"))
        return;

    const auto& linfo = data["lighting_info"];

    auto parse_light = [](const nlohmann::json& l) -> std::optional<LightSource> {
        if (!l.is_object() || !l.value("has_light_source", false))
            return std::nullopt;

        LightSource light;
        light.intensity = l.value("light_intensity", 0);
        light.radius    = l.value("radius", 100);
        light.fall_off  = l.value("fall_off", 0);
        light.flare     = l.value("flare", 1);
        light.flicker   = l.value("flicker", 0);
        light.offset_x  = l.value("offset_x", 0);
        light.offset_y  = l.value("offset_y", 0);
        light.x_radius  = l.value("x_radius", 0);
        light.y_radius  = l.value("y_radius", 0);
        double factor   = l.value("factor", 100);
        light.color     = {255, 255, 255, 255};
        factor = factor/100;
        light.x_radius = light.x_radius * factor;
        light.y_radius = light.y_radius * factor;

        if (l.contains("light_color") && l["light_color"].is_array() && l["light_color"].size() == 3) {
            light.color.r = l["light_color"][0].get<int>();
            light.color.g = l["light_color"][1].get<int>();
            light.color.b = l["light_color"][2].get<int>();
        }

        return light;
    };

    if (linfo.is_array()) {
        for (const auto& l : linfo) {
            auto maybe = parse_light(l);
            if (maybe.has_value()) {
                has_light_source = true;
                LightSource light = maybe.value();

                
                if (light.x_radius > 0 || light.y_radius > 0)
                    orbital_light_sources.push_back(light);
                else
                    light_sources.push_back(light);
            }
        }
    } else if (linfo.is_object()) {
        auto maybe = parse_light(linfo);
        if (maybe.has_value()) {
            has_light_source = true;
            LightSource light = maybe.value();

            if (light.x_radius > 0 || light.y_radius > 0)
                orbital_light_sources.push_back(light);
            else
                light_sources.push_back(light);
        }
    }
}


void AssetInfo::load_collision_areas(const nlohmann::json& data,
                                     const std::string& dir_path,
                                     int offset_x,
                                     int offset_y) {
    try_load_area(data, "impassable_area", dir_path, passability_area, has_passability_area, scale_factor, offset_x, offset_y);
    try_load_area(data, "spacing_area", dir_path, spacing_area, has_spacing_area, scale_factor, offset_x, offset_y);
    try_load_area(data, "collision_area", dir_path, collision_area, has_collision_area, scale_factor, offset_x, offset_y);
    try_load_area(data, "interaction_area", dir_path, interaction_area, has_interaction_area, scale_factor, offset_x, offset_y);
    try_load_area(data, "hit_area", dir_path, attack_area, has_attack_area, scale_factor, offset_x, offset_y);
}

void AssetInfo::load_child_json_paths(const nlohmann::json& data,
                                      const std::string& dir_path)
{
    children.clear();
    if (!data.contains("child_assets") || !data["child_assets"].is_array())
        return;

    for (const auto& entry : data["child_assets"]) {
        std::string rel_path;
        if (entry.is_string()) {
            rel_path = entry.get<std::string>();
        }
        else if (entry.is_object() && entry.contains("json_path") && entry["json_path"].is_string()) {
            rel_path = entry["json_path"].get<std::string>();
        } else {
            continue;
        }

        fs::path full_path = fs::path(dir_path) / rel_path;
        if (!fs::exists(full_path)) {
            std::cerr << "[AssetInfo] child JSON not found: " << full_path << "\n";
            continue;
        }

        
        int z_offset_value = 0;
        try {
            std::ifstream in(full_path);
            nlohmann::json childJson;
            in >> childJson;
            if (childJson.contains("z_offset")) {
                auto& jz = childJson["z_offset"];
                if (jz.is_number()) {
                    z_offset_value = static_cast<int>(jz.get<double>());
                } else if (jz.is_string()) {
                    try { z_offset_value = std::stoi(jz.get<std::string>()); } catch (...) {}
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[AssetInfo] failed to parse z_offset from "
                      << full_path << " | " << e.what() << "\n";
        }

        ChildInfo ci;
        ci.json_path = full_path.string();
        ci.z_offset  = z_offset_value;

        try {
            std::string area_name = fs::path(rel_path).stem().string();
            ci.area_ptr = std::make_unique<Area>(area_name, full_path.string(), scale_factor);
            ci.area_ptr->apply_offset(0, 100);
            ci.has_area = true;
            std::cout << "[AssetInfo] loaded child area from: "
                      << full_path.string() << "\n";
        } catch (const std::exception& e) {
            std::cerr << "[AssetInfo] failed to construct area from child JSON: "
                      << full_path << " | " << e.what() << "\n";
            ci.has_area = false;
        }

        children.emplace_back(std::move(ci));
    }
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

void AssetInfo::generate_lights(SDL_Renderer* renderer) {
    GenerateLight generator(renderer);

    for (std::size_t i = 0; i < light_sources.size(); ++i) {
        SDL_Texture* tex = generator.generate(renderer, name, light_sources[i], i);
        if (tex) {
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
            light_sources[i].texture = tex;

            
            SDL_QueryTexture(tex, nullptr, nullptr, &light_sources[i].cached_w, &light_sources[i].cached_h);
        }
    }

    std::size_t base_index = light_sources.size();
    for (std::size_t i = 0; i < orbital_light_sources.size(); ++i) {
        SDL_Texture* tex = generator.generate(renderer, name, orbital_light_sources[i], base_index + i);
        if (tex) {
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
            orbital_light_sources[i].texture = tex;

            
            SDL_QueryTexture(tex, nullptr, nullptr, &orbital_light_sources[i].cached_w, &orbital_light_sources[i].cached_h);
        }
    }
}
