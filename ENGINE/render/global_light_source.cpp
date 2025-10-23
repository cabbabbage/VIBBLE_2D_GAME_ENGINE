#include "global_light_source.hpp"
#include "map_generation/map_layers_geometry.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <random>
#include <optional>
#include <SDL.h>

#include "utils/ranged_color.hpp"
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
        base_color_range_ = utils::color::RangedColor{
            {fallback_base_color.r, fallback_base_color.r},
            {fallback_base_color.g, fallback_base_color.g},
            {fallback_base_color.b, fallback_base_color.b},
            {fallback_base_color.a, fallback_base_color.a}
        };
        base_color_      = clamp_color_alpha(fallback_base_color);
        current_color_   = base_color_;
        key_colors_.clear();
        key_colors_.push_back({0.0f, base_color_range_, base_color_, false});
        resolve_each_orbit_ = false;
        base_pending_resolve_ = false;
        last_degree_ = 0.0f;
        active_segment_start_ = 0;
        active_segment_end_ = 0;
        orbit_initialized_ = false;
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

        if (auto parsed = utils::color::ranged_color_from_json(data.value("base_color", nlohmann::json{}))) {
                base_color_range_ = *parsed;
        }
        base_color_ = clamp_color_alpha(utils::color::resolve_ranged_color(base_color_range_));

        key_colors_.clear();
        const auto keys_it = data.find("keys");
        if (keys_it != data.end() && keys_it->is_array()) {
                for (const auto& entry : *keys_it) {
                        if (!entry.is_array() || entry.size() < 2) continue;
                        float deg = 0.0f;
                        try {
                                deg = static_cast<float>(entry[0].get<double>());
                        } catch (...) {
                                continue;
                        }
                        utils::color::RangedColor range = base_color_range_;
                        if (auto parsed = utils::color::ranged_color_from_json(entry[1])) {
                                range = *parsed;
                        }
                        KeyEntry key{};
                        key.degree = deg;
                        key.range = range;
                        resolve_key_entry(key);
                        key_colors_.push_back(key);
                }
        }
        if (key_colors_.empty()) {
                KeyEntry key{};
                key.degree = 0.0f;
                key.range = base_color_range_;
                resolve_key_entry(key);
                key_colors_.push_back(key);
        } else {
                std::sort(key_colors_.begin(), key_colors_.end(), [](const KeyEntry& a, const KeyEntry& b) {
                        return a.degree < b.degree;
                });
        }
        resolve_each_orbit_ = key_colors_.size() > 2;
        base_pending_resolve_ = false;
        for (auto& key : key_colors_) {
                key.needs_resolve = false;
        }
        last_degree_ = 0.0f;
        active_segment_start_ = 0;
        active_segment_end_ = key_colors_.size() > 1 ? 1 : 0;
        orbit_initialized_ = false;

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

void Global_Light_Source::resolve_key_entry(KeyEntry& entry) {
        entry.color = clamp_color_alpha(utils::color::resolve_ranged_color(entry.range));
        entry.needs_resolve = false;
}

void Global_Light_Source::update_active_segment(float degree) {
        if (key_colors_.empty()) {
                active_segment_start_ = 0;
                active_segment_end_ = 0;
                return;
        }
        if (key_colors_.size() == 1) {
                active_segment_start_ = active_segment_end_ = 0;
                return;
        }
        for (size_t i = 0; i + 1 < key_colors_.size(); ++i) {
                const auto& K0 = key_colors_[i];
                const auto& K1 = key_colors_[i + 1];
                if (degree >= K0.degree && degree <= K1.degree) {
                        active_segment_start_ = i;
                        active_segment_end_ = i + 1;
                        return;
                }
        }
        active_segment_start_ = key_colors_.size() - 1;
        active_segment_end_ = 0;
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

        float deg = std::fmod(angle_ * (180.0f/float(M_PI)) + 270.0f, 360.0f);
        if (deg < 0) deg += 360.0f;

        if (resolve_each_orbit_) {
                if (orbit_initialized_ && deg > last_degree_ + 180.0f) {
                        base_pending_resolve_ = true;
                        for (auto& key : key_colors_) {
                                key.needs_resolve = true;
                        }
                }
                last_degree_ = deg;
        }

        update_active_segment(deg);

        if (resolve_each_orbit_) {
                if (base_pending_resolve_) {
                        base_color_ = clamp_color_alpha(utils::color::resolve_ranged_color(base_color_range_));
                        base_pending_resolve_ = false;
                }
                for (size_t i = 0; i < key_colors_.size(); ++i) {
                        if (!key_colors_[i].needs_resolve) {
                                continue;
                        }
                        if (i == active_segment_start_ || i == active_segment_end_) {
                                continue;
                        }
                        resolve_key_entry(key_colors_[i]);
                }
        }
        orbit_initialized_ = true;

        SDL_Color k = compute_color_from_horizon(deg);
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
        const int alpha = std::clamp(static_cast<int>(current_color_.a), 0, 255);
        light_brightness = std::clamp(255 - alpha, 0, 255);
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

SDL_Color Global_Light_Source::compute_color_from_horizon(float degree) const {
        auto lerp = [](Uint8 A, Uint8 B, float t){
                return Uint8(A + (B - A) * t);
};

        if (key_colors_.size() < 2) {
                return key_colors_.empty() ? base_color_ : key_colors_.front().color;
        }

        for (size_t i = 0; i + 1 < key_colors_.size(); ++i) {
                auto &K0 = key_colors_[i], &K1 = key_colors_[i+1];
                if (degree >= K0.degree && degree <= K1.degree) {
                        float span = K1.degree - K0.degree;
                        float t = span <= 0.0f ? 0.0f : (degree - K0.degree) / span;
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
        float t = (degree < KF.degree) ? (degree + 360.0f - KL.degree) / span : (degree - KL.degree) / span;

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
        v = std::clamp(v, 0, 255);
        return static_cast<Uint8>(v);
}

SDL_Color Global_Light_Source::clamp_color_alpha(SDL_Color color) const {
        color.a = clamp_alpha(color.a);
        return color;
}
