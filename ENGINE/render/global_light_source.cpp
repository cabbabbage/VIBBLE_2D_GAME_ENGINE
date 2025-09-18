#include "global_light_source.hpp"
#include "generate_light.hpp"
#include "utils/light_source.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <random>
#include <SDL.h>
using json = nlohmann::json;

Global_Light_Source::Global_Light_Source(SDL_Renderer* renderer,
                                         SDL_Point screen_center,
                                         int screen_width,
                                         SDL_Color fallback_base_color,
                                         const std::string& map_path)
: renderer_(renderer),
texture_(nullptr),
base_color_(fallback_base_color),
current_color_(fallback_base_color),
center_(screen_center),
angle_(0.0f),
initialized_(false),
pos_{0,0},
frame_counter_(0),
light_brightness(255)
{
        set_defaults(screen_width, fallback_base_color);
        if (!load_from_map_light(map_path)) {
                build_texture();
                set_light_brightness();
        }
}

void Global_Light_Source::set_defaults(int screen_width, SDL_Color fallback_base_color) {
        radius_          = float(screen_width) * 3.0f;
        intensity_       = 255.0f;
        mult_            = 0.4f;
        fall_off_        = 1.0f;
        orbit_radius     = std::max(1, screen_width / 4);
        update_interval_ = 2;
        base_color_      = fallback_base_color;
        current_color_   = fallback_base_color;
        key_colors_.clear();
        key_colors_.push_back({0.0f, fallback_base_color});
}

bool Global_Light_Source::load_from_map_light(const std::string& map_path) {
        if (map_path.empty()) {
                return false;
        }
        std::ifstream in(map_path + "/map_light.json");
        if (!in.is_open()) {
                std::cerr << "[MapLight] Failed to open map_light.json in " << map_path << "\n";
                return false;
        }
        json j;
        try {
                in >> j;
        } catch (const std::exception& e) {
                std::cerr << "[MapLight] Failed to parse map_light.json: " << e.what() << "\n";
                return false;
        }
        apply_config(j);
        return true;
}

void Global_Light_Source::apply_config(const json& data) {
        if (!data.is_object()) {
                return;
        }

        radius_        = data.value("radius", radius_);
        intensity_     = data.value("intensity", intensity_);
        orbit_radius   = data.value("orbit_radius", orbit_radius);
        update_interval_= std::max(1, data.value("update_interval", update_interval_));
        mult_          = std::clamp(data.value("mult", mult_), 0.0f, 1.0f);
        fall_off_      = data.value("fall_off", fall_off_);

        const auto bc_it = data.find("base_color");
        if (bc_it != data.end() && bc_it->is_array() && bc_it->size() >= 3) {
                base_color_.r = static_cast<Uint8>(std::clamp((*bc_it)[0].get<int>(), 0, 255));
                base_color_.g = static_cast<Uint8>(std::clamp((*bc_it)[1].get<int>(), 0, 255));
                base_color_.b = static_cast<Uint8>(std::clamp((*bc_it)[2].get<int>(), 0, 255));
                if (bc_it->size() >= 4) {
                        base_color_.a = static_cast<Uint8>(std::clamp((*bc_it)[3].get<int>(), 0, 255));
                } else {
                        base_color_.a = 255;
                }
        }

        key_colors_.clear();
        const auto keys_it = data.find("keys");
        if (keys_it != data.end() && keys_it->is_array()) {
                for (const auto& entry : *keys_it) {
                        if (!entry.is_array() || entry.size() != 2) continue;
                        float deg = 0.0f;
                        try {
                                deg = static_cast<float>(entry[0].get<double>());
                        } catch (...) {
                                continue;
                        }
                        const auto& col = entry[1];
                        if (!col.is_array() || col.size() < 4) continue;
                        SDL_Color c{
                                static_cast<Uint8>(std::clamp(col[0].get<int>(), 0, 255)),
                                static_cast<Uint8>(std::clamp(col[1].get<int>(), 0, 255)),
                                static_cast<Uint8>(std::clamp(col[2].get<int>(), 0, 255)),
                                static_cast<Uint8>(std::clamp(col[3].get<int>(), 0, 255))
                        };
                        key_colors_.push_back({deg, c});
                }
        }
        if (key_colors_.empty()) {
                key_colors_.push_back({0.0f, base_color_});
        } else {
                std::sort(key_colors_.begin(), key_colors_.end(), [](const KeyEntry& a, const KeyEntry& b) {
                        return a.degree < b.degree;
                });
        }

        current_color_ = base_color_;
        build_texture();
        set_light_brightness();
}

void Global_Light_Source::update() {
	if (++frame_counter_ % update_interval_ != 0) {
		return;
	}
	if (!initialized_) {
		static thread_local std::mt19937 rng{std::random_device{}()};
		std::uniform_real_distribution<float> dist(0.0f, 2.0f * float(M_PI));
		angle_ = dist(rng);
		initialized_ = true;
	}

	float prev = angle_;
	angle_ -= 0.01f;
	if (angle_ < 0.0f) angle_ += 2.0f * float(M_PI);

	float ca = std::cos(angle_), sa = std::sin(angle_);
	pos_.x = center_.x + int(orbit_radius * ca);
	pos_.y = center_.y - int(orbit_radius * sa);

	SDL_Color k = compute_color_from_horizon();
	current_color_ = k;
	set_light_brightness();
}

SDL_Point Global_Light_Source::get_position() const {
	return pos_;
}

float Global_Light_Source::get_angle() const {
	return angle_;
}

SDL_Texture* Global_Light_Source::get_texture() const {
	return texture_;
}

void Global_Light_Source::set_light_brightness() {
	constexpr int OFF = 245, FULL = 100;
	int a = current_color_.a;
	if (a >= OFF) {
		light_brightness = 0;
	} else if (a <= FULL) {
		light_brightness = 255;
	} else {
		float r = float(OFF - a) / float(OFF - FULL);
		light_brightness = int(r * 255.0f);
	}
}

void Global_Light_Source::build_texture() {
	if (texture_) SDL_DestroyTexture(texture_);
	LightSource ls;
	ls.radius    = int(radius_);
	ls.intensity = int(intensity_);
	ls.fall_off  = int(fall_off_);
	ls.flare     = 0;
	ls.color     = base_color_;
	GenerateLight gen(renderer_);
	texture_ = gen.generate(renderer_, "map", ls, 0);
	if (!texture_) {
		std::cerr << "[MapLight] build_texture failed\n";
		cached_w_ = cached_h_ = 0;
	} else {
		SDL_QueryTexture(texture_, nullptr, nullptr, &cached_w_, &cached_h_);
	}
}

SDL_Color Global_Light_Source::compute_color_from_horizon() const {
	float deg = std::fmod(angle_ * (180.0f/float(M_PI)) + 270.0f, 360.0f);
	if (deg < 0) deg += 360.0f;

	auto lerp = [](Uint8 A, Uint8 B, float t){
		return Uint8(A + (B - A) * t);
	};

	if (key_colors_.size() < 2) {
		return key_colors_.empty() ? base_color_ : key_colors_.front().color;
	}

	for (size_t i = 0; i + 1 < key_colors_.size(); ++i) {
		auto &K0 = key_colors_[i], &K1 = key_colors_[i+1];
		if (deg >= K0.degree && deg <= K1.degree) {
			float t = (deg - K0.degree) / (K1.degree - K0.degree);
			return {
				lerp(K0.color.r, K1.color.r, t),
				lerp(K0.color.g, K1.color.g, t),
				lerp(K0.color.b, K1.color.b, t),
				lerp(K0.color.a, K1.color.a, t)
			};
		}
	}

	auto &KL = key_colors_.back(), &KF = key_colors_.front();
	float span = 360.0f - KL.degree + KF.degree;
	float t = (deg < KF.degree) ? (deg + 360.0f - KL.degree) / span : (deg - KL.degree) / span;

	return {
		lerp(KL.color.r, KF.color.r, t),
		lerp(KL.color.g, KF.color.g, t),
		lerp(KL.color.b, KF.color.b, t),
		lerp(KL.color.a, KF.color.a, t)
	};
}

SDL_Color Global_Light_Source::get_current_color() const {
	return current_color_;
}

int Global_Light_Source::get_brightness() const {
	return light_brightness;
}

Global_Light_Source::~Global_Light_Source() {
	if (texture_) {
		SDL_DestroyTexture(texture_);
		texture_ = nullptr;
	}
}
