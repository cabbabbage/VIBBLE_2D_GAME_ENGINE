#pragma once

#include <vector>
#include <string>
#include <utility>
#include <SDL.h>
#include <nlohmann/json.hpp>

class AssetInfo;

class Animation {

	public:
    Animation();
    struct FrameMovement {
    int dx = 0;
    int dy = 0;
    bool sort_z_index = true;
    SDL_Color rgb{255, 255, 255, 255};
	};
    void load(const std::string& trigger, const nlohmann::json& anim_json, class AssetInfo& info, const std::string& dir_path, const std::string& root_cache, float scale_factor, SDL_Renderer* renderer, SDL_Texture*& base_sprite, int& scaled_sprite_w, int& scaled_sprite_h, int& original_canvas_width, int& original_canvas_height);
    SDL_Texture* get_frame(int index) const;
    bool advance(int& index, float& progress, int& dx, int& dy, bool& resort_z) const;
    void change(int& index, bool& static_flag) const;
    void freeze();
    bool is_frozen() const;
    bool is_static() const;
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
    std::vector<FrameMovement> movement;
    int total_dx = 0;
    int total_dy = 0;
    bool movment = false;
    bool rnd_start = false;
    std::string on_end_mapping;
    std::string on_end_animation;
    std::vector<SDL_Texture*> frames;
    bool randomize = false;
    bool loop = true;
    bool frozen = false;

	private:
};
