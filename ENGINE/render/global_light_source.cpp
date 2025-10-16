#include "global_light_source.hpp"
#include "map_generation/map_layers_geometry.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <random>
#include <optional>
#include <SDL.h>
using json = nlohmann::json;

Global_Light_Source::Global_Light_Source(SDL_Renderer* renderer,
                                         SDL_Point screen_center,
                                         int screen_width,
                                         SDL_Color fallback_base_color)
: renderer_(renderer),
        base_color_(fallback_base_color),
        current_color_(fallback_base_color),
        default_center_(screen_center),
        default_map_center_(screen_center),
        center_(screen_center),
        map_reference_center_(screen_center),
        angle_(0.0f),
        initialized_(false),
        pos_{screen_center.x, screen_center.y},
        frame_counter_(0),
        light_brightness(255),
        orbit_radius_x_(0),
        orbit_radius_y_(0)
{
        set_defaults(screen_width, fallback_base_color);
        set_light_brightness();
        recalc_position();
}

bool Global_Light_Source::initialize_from_map_manifest(const json& map_info, std::string_view map_id) {
        return load_from_map_manifest(map_info, map_id);
}

void Global_Light_Source::set_defaults(int screen_width, SDL_Color fallback_base_color) {
        radius_          = float(screen_width) * 3.0f;
        intensity_       = 255.0f;
        mult_            = 0.4f;
        fall_off_        = 1.0f;
        orbit_radius_x_  = std::max(1, screen_width / 4);
        orbit_radius_y_  = orbit_radius_x_;
        update_interval_ = 2;
        base_color_      = clamp_color_alpha(fallback_base_color);
        current_color_   = base_color_;
        min_opacity_     = 0;
        max_opacity_     = 255;
        key_colors_.clear();
        key_colors_.push_back({0.0f, base_color_});
        center_ = default_center_;
        default_map_center_ = default_center_;
        map_reference_center_ = default_map_center_;
        recalc_position();
}

bool Global_Light_Source::load_from_map_manifest(const json& map_info, std::string_view map_id) {
        if (!map_info.is_object()) {
                std::cerr << "[MapLight] Map manifest for '" << map_id << "' is not an object. Using defaults.\n";
                set_light_brightness();
                recalc_position();
                return false;
        }

        int default_cx = default_map_center_.x;
        int default_cy = default_map_center_.y;
        try {
                const double map_radius = map_layers::map_radius_from_map_info(map_info);
                if (map_radius > 0.0) {
                        const int center_val = static_cast<int>(std::lround(map_radius));
                        default_cx = center_val;
                        default_cy = center_val;
                }
        } catch (...) {
        }

        default_map_center_ = SDL_Point{ default_cx, default_cy };
        map_reference_center_ = default_map_center_;
        center_ = default_center_;

        auto it = map_info.find("map_light_data");
        if (it == map_info.end() || !it->is_object()) {
                if (!map_id.empty()) {
                        std::cerr << "[MapLight] Manifest for '" << map_id << "' has no valid map_light_data object. Using defaults.\n";
                } else {
                        std::cerr << "[MapLight] Manifest has no valid map_light_data object. Using defaults.\n";
                }
                set_light_brightness();
                recalc_position();
                return false;
        }

        apply_config(*it);
        return true;
}

void Global_Light_Source::apply_config(const json& data) {
        if (!data.is_object()) {
                return;
        }

        radius_        = data.value("radius", radius_);
        intensity_     = data.value("intensity", intensity_);
        const int fallback_orbit = std::clamp(data.value("orbit_radius", orbit_radius_x_), 0, 20000);
        orbit_radius_x_ = std::clamp(data.value("orbit_x", fallback_orbit), 0, 20000);
        orbit_radius_y_ = std::clamp(data.value("orbit_y", orbit_radius_x_), 0, 20000);
        update_interval_= std::max(1, data.value("update_interval", update_interval_));
        mult_          = std::clamp(data.value("mult", mult_), 0.0f, 1.0f);
        fall_off_      = data.value("fall_off", fall_off_);

        min_opacity_ = std::clamp(data.value("min_opacity", min_opacity_), 0, 255);
        max_opacity_ = std::clamp(data.value("max_opacity", max_opacity_), 0, 255);
        if (min_opacity_ > max_opacity_) {
                std::swap(min_opacity_, max_opacity_);
        }

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
        base_color_ = clamp_color_alpha(base_color_);

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
                                static_cast<Uint8>(std::clamp(col[0].get<int>(), 0, 255)), static_cast<Uint8>(std::clamp(col[1].get<int>(), 0, 255)), static_cast<Uint8>(std::clamp(col[2].get<int>(), 0, 255)), static_cast<Uint8>(std::clamp(col[3].get<int>(), 0, 255)) };
                        key_colors_.push_back({deg, clamp_color_alpha(c)});
                }
        }
        if (key_colors_.empty()) {
                key_colors_.push_back({0.0f, base_color_});
        } else {
                std::sort(key_colors_.begin(), key_colors_.end(), [](const KeyEntry& a, const KeyEntry& b) {
                        return a.degree < b.degree;
                });
        }

        center_ = default_center_;
        map_reference_center_ = default_map_center_;
        auto parse_point = [](const nlohmann::json& arr) -> std::optional<SDL_Point> {
                if (!arr.is_array() || arr.size() < 2) {
                        return std::nullopt;
                }
                try {
                        double cx = arr[0].get<double>();
                        double cy = arr[1].get<double>();
                        return SDL_Point{ static_cast<int>(std::lround(cx)), static_cast<int>(std::lround(cy)) };
                } catch (...) {
                        return std::nullopt;
                }
};
        bool custom_center = false;
        if (auto center_it = data.find("center"); center_it != data.end()) {
                if (auto parsed = parse_point(*center_it)) {
                        map_reference_center_ = *parsed;
                        custom_center = true;
                }
        }
        if (!custom_center) {
                if (auto position_it = data.find("position"); position_it != data.end()) {
                        if (auto parsed = parse_point(*position_it)) {
                                map_reference_center_ = *parsed;
                                custom_center = true;
                        }
                }
        }
        if (!custom_center) {
                if (data.contains("center_x")) {
                        try {
                                map_reference_center_.x = data.at("center_x").get<int>();
                                custom_center = true;
                        } catch (...) {}
                }
                if (data.contains("center_y")) {
                        try {
                                map_reference_center_.y = data.at("center_y").get<int>();
                                custom_center = true;
                        } catch (...) {}
                }
        }

        current_color_ = clamp_color_alpha(base_color_);
        set_light_brightness();
        recalc_position();
        frame_counter_ = 0;
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

        angle_ -= 0.01f;
	if (angle_ < 0.0f) angle_ += 2.0f * float(M_PI);

	recalc_position();

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

void Global_Light_Source::set_light_brightness() {
        const int alpha = static_cast<int>(current_color_.a);
        if (alpha <= min_opacity_) {
                light_brightness = 255;
                return;
        }
        if (alpha >= max_opacity_) {
                light_brightness = 0;
                return;
        }
        const int range = std::max(1, max_opacity_ - min_opacity_);
        const float scaled = static_cast<float>(max_opacity_ - alpha) / static_cast<float>(range);
        light_brightness = static_cast<int>(std::clamp(scaled * 255.0f, 0.0f, 255.0f));
}

void Global_Light_Source::recalc_position() {
        const double ca = std::cos(angle_);
        const double sa = std::sin(angle_);
        const int dx = static_cast<int>(std::lround(static_cast<double>(orbit_radius_x_) * ca));
        const int dy = static_cast<int>(std::lround(static_cast<double>(orbit_radius_y_) * sa));
        pos_.x = center_.x + dx;
        pos_.y = center_.y - dy;
}

SDL_Point Global_Light_Source::get_direction_target() const {
        const int offset_x = pos_.x - center_.x;
        const int offset_y = pos_.y - center_.y;
        return SDL_Point{ map_reference_center_.x + offset_x, map_reference_center_.y + offset_y };
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
                        return clamp_color_alpha({
                                lerp(K0.color.r, K1.color.r, t),
                                lerp(K0.color.g, K1.color.g, t),
                                lerp(K0.color.b, K1.color.b, t),
                                lerp(K0.color.a, K1.color.a, t)
                        });
                }
        }

        auto &KL = key_colors_.back(), &KF = key_colors_.front();
        float span = 360.0f - KL.degree + KF.degree;
        float t = (deg < KF.degree) ? (deg + 360.0f - KL.degree) / span : (deg - KL.degree) / span;

        return clamp_color_alpha({
                lerp(KL.color.r, KF.color.r, t),
                lerp(KL.color.g, KF.color.g, t),
                lerp(KL.color.b, KF.color.b, t),
                lerp(KL.color.a, KF.color.a, t)
        });
}

SDL_Color Global_Light_Source::get_current_color() const {
        return current_color_;
}

int Global_Light_Source::get_brightness() const {
        return light_brightness;
}

Uint8 Global_Light_Source::clamp_alpha(Uint8 value) const {
        int v = static_cast<int>(value);
        v = std::clamp(v, min_opacity_, max_opacity_);
        return static_cast<Uint8>(v);
}

SDL_Color Global_Light_Source::clamp_color_alpha(SDL_Color color) const {
        color.a = clamp_alpha(color.a);
        return color;
}
