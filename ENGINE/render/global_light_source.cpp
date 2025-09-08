#include "global_light_source.hpp"
#include "generate_light.hpp"
#include "utils/light_source.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <random>
using json = nlohmann::json;

Global_Light_Source::Global_Light_Source(SDL_Renderer* renderer,
int screen_center_x,
int screen_center_y,
int screen_width,
SDL_Color fallback_base_color,
const std::string& map_path)
: renderer_(renderer),
texture_(nullptr),
base_color_(fallback_base_color),
current_color_(fallback_base_color),
center_x_(screen_center_x),
center_y_(screen_center_y),
angle_(0.0f),
initialized_(false),
pos_x_(0),
pos_y_(0),
frame_counter_(0),
light_brightness(255)
{
	radius_          = float(screen_width) * 3.0f;
	intensity_       = 255.0f;
	mult_            = 0.4f;
	fall_off_        = 1.0f;
	orbit_radius     = screen_width / 4;
	update_interval_ = 2;
	std::ifstream in(map_path + "/map_light.json");
	if (!in.is_open()) {
		throw std::runtime_error("[MapLight] Failed to open map_light.json");
	}
	json j; in >> j;
	for (auto& key : {"radius","intensity","orbit_radius","update_interval","mult","fall_off","base_color","keys"}) {
		if (!j.contains(key))
		throw std::runtime_error(std::string("[MapLight] Missing field: ") + key);
	}
	radius_          = j["radius"].get<float>();
	intensity_       = j["intensity"].get<float>();
	orbit_radius     = j["orbit_radius"].get<int>();
	update_interval_ = j["update_interval"].get<int>();
	mult_            = j["mult"].get<float>();
	fall_off_        = j["fall_off"].get<float>();
	auto& bc = j["base_color"];
	if (!bc.is_array() || bc.size() < 3)
	throw std::runtime_error("[MapLight] Invalid base_color");
	base_color_.r = bc[0]; base_color_.g = bc[1]; base_color_.b = bc[2];
	if (bc.size() > 3)
	base_color_.a = static_cast<Uint8>(bc[3].get<int>());
	else
	base_color_.a = 255;
	current_color_ = base_color_;
	key_colors_.clear();
	for (auto& entry : j["keys"]) {
		if (!entry.is_array() || entry.size()!=2)
		throw std::runtime_error("[MapLight] Invalid key entry");
		float deg = entry[0].get<float>();
		auto& col = entry[1];
		if (!col.is_array() || col.size()!=4)
		throw std::runtime_error("[MapLight] Invalid key color");
		SDL_Color c{ Uint8(col[0]), Uint8(col[1]), Uint8(col[2]), Uint8(col[3]) };
		key_colors_.push_back({deg,c});
	}
	build_texture();
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
	pos_x_ = center_x_ + int(orbit_radius * ca);
	pos_y_ = center_y_ - int(orbit_radius * sa);
	SDL_Color k = compute_color_from_horizon();
	current_color_ = k;
	set_light_brightness();
}

std::pair<int,int> Global_Light_Source::get_position() const {
	return { pos_x_, pos_y_ };
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
	float t = (deg < KF.degree)
	? (deg + 360.0f - KL.degree) / span
	: (deg - KL.degree) / span;
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
