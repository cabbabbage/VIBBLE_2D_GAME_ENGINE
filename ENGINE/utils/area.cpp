#include "area.hpp"
#include "cache_manager.hpp"
#include <fstream>
#include <nlohmann/json.hpp>
#include <random>
#include <cmath>
#include <algorithm>
#include <array>
#include <stdexcept>
#include <filesystem>
#include <sstream>
#include <optional>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static std::mt19937 rng{std::random_device{}()};

Area::Area(const std::string& name, int resolution)
: pos{0, 0}, area_name_(name)
{
        resolution_ = vibble::grid::clamp_resolution(resolution);
        apply_resolution_to_points();
}

Area::Area(const std::string& name, const std::vector<Point>& pts, int resolution)
: points(pts), area_name_(name)
{
        resolution_ = vibble::grid::clamp_resolution(resolution);
        apply_resolution_to_points();
        update_geometry_data();
        if (!points.empty()) {
                auto [minx, miny, maxx, maxy] = get_bounds();
                pos.x = (minx + maxx) / 2;
                pos.y = maxy;
        }
}

Area::Area(const std::string& name, SDL_Point center, int w, int h,
           const std::string& geometry,
           int edge_smoothness,
           int map_width, int map_height,
           int resolution)
: area_name_(name)
{
        if (w <= 0 || h <= 0 || map_width <= 0 || map_height <= 0) {
                throw std::runtime_error("[Area: " + area_name_ + "] Invalid dimensions");
        }
        resolution_ = vibble::grid::clamp_resolution(resolution);
        if (geometry == "Circle") {
                generate_circle(center, w / 2, edge_smoothness, map_width, map_height);
        } else if (geometry == "Square") {
                generate_square(center, w, h, edge_smoothness, map_width, map_height);
        } else if (geometry == "Point") {
                generate_point(center, map_width, map_height);
        } else {
                throw std::runtime_error("[Area: " + area_name_ + "] Unknown geometry: " + geometry);
        }
        update_geometry_data();
        if (!points.empty()) {
                auto [minx, miny, maxx, maxy] = get_bounds();
                pos.x = (minx + maxx) / 2;
                pos.y = maxy;
        }
}

Area::Area(const std::string& name, const std::string& json_path, float )
: area_name_(name)
{
        std::ifstream in(json_path);
        if (!in.is_open()) {
                throw std::runtime_error("[Area: " + area_name_ + "] Failed to open JSON: " + json_path);
        }
        nlohmann::json j;
        in >> j;
        if (!j.contains("points") || !j["points"].is_array()) {
                throw std::runtime_error("[Area: " + area_name_ + "] Bad JSON: " + json_path);
        }
        int resolution = 2;
        if (j.contains("resolution") && j["resolution"].is_number_integer()) {
                resolution = j["resolution"].get<int>();
        }
        resolution_ = vibble::grid::clamp_resolution(resolution);
        SDL_Point anchor{0, 0};
        if (j.contains("anchor") && j["anchor"].is_object()) {
                anchor.x = j["anchor"].value("x", 0);
                anchor.y = j["anchor"].value("y", 0);
        }
        points.clear();
        points.reserve(j["points"].size());
        for (const auto& elem : j["points"]) {
                if (!elem.is_object()) continue;
                int x = elem.value("x", 0);
                int y = elem.value("y", 0);
                points.push_back({ anchor.x + x, anchor.y + y });
        }
        if (points.empty()) {
                throw std::runtime_error("[Area: " + area_name_ + "] No points loaded");
        }
        pos = vibble::grid::snap_world_to_vertex(anchor, resolution_);
        apply_resolution_to_points();
        update_geometry_data();
}

void Area::apply_offset(int dx, int dy) {
        bounds_valid_ = false;
        for (auto& p : points) {
                p.x += dx;
                p.y += dy;
        }
        pos.x += dx;
        pos.y += dy;
        apply_resolution_to_points();
        update_geometry_data();
}

void Area::align(SDL_Point target) {
        int dx = target.x - pos.x;
        int dy = target.y - pos.y;
        apply_offset(dx, dy);
}

std::tuple<int, int, int, int> Area::get_bounds() const {
	if (bounds_valid_) {
		return {min_x_, min_y_, max_x_, max_y_};
	}
	if (points.empty())
	throw std::runtime_error("[Area: " + area_name_ + "] get_bounds() on empty point set");
        int minx = points[0].x, maxx = minx;
        int miny = points[0].y, maxy = miny;
        for (const auto& p : points) {
                minx = std::min(minx, p.x);
                maxx = std::max(maxx, p.x);
                miny = std::min(miny, p.y);
                maxy = std::max(maxy, p.y);
        }
	min_x_ = minx; min_y_ = miny; max_x_ = maxx; max_y_ = maxy;
	bounds_valid_ = true;
	return {minx, miny, maxx, maxy};
}

void Area::generate_point(SDL_Point center, int map_width, int map_height) {
        points.clear();
        points.emplace_back(SDL_Point{ std::clamp(center.x, 0, map_width), std::clamp(center.y, 0, map_height) });
        bounds_valid_ = false;
        apply_resolution_to_points();
}

void Area::generate_circle(SDL_Point center, int radius, int edge_smoothness, int map_width, int map_height) {
        int s = std::clamp(edge_smoothness, 0, 100);
        int count = std::max(12, 6 + s * 2);
	double max_dev = 0.20 * (100 - s) / 100.0;
	std::uniform_real_distribution<double> dist(1.0 - max_dev, 1.0 + max_dev);
	points.clear();
	points.reserve(count);
	for (int i = 0; i < count; ++i) {
		double theta = 2 * M_PI * i / count;
		double rx = radius * dist(rng), ry = radius * dist(rng);
		double x = center.x + rx * std::cos(theta);
		double y = center.y + ry * std::sin(theta);
                int xi = static_cast<int>(std::round(std::clamp(x, 0.0, static_cast<double>(map_width))));
                int yi = static_cast<int>(std::round(std::clamp(y, 0.0, static_cast<double>(map_height))));
                points.emplace_back(SDL_Point{ xi, yi });
        }
        bounds_valid_ = false;
        apply_resolution_to_points();
}

void Area::generate_square(SDL_Point center, int w, int h, int edge_smoothness, int map_width, int map_height) {
        int s = std::clamp(edge_smoothness, 0, 100);
        double max_dev = 0.25 * (100 - s) / 100.0;
	std::uniform_real_distribution<double> xoff(-max_dev * w, max_dev * w);
	std::uniform_real_distribution<double> yoff(-max_dev * h, max_dev * h);
	int half_w = w / 2, half_h = h / 2;
	points.clear();
	points.reserve(4);
        for (auto [x0, y0] : std::array<Point, 4>{
      Point{center.x - half_w, center.y - half_h},
      Point{center.x + half_w, center.y - half_h},
      Point{center.x + half_w, center.y + half_h},
      Point{center.x - half_w, center.y + half_h}}) {
                int x = static_cast<int>(std::round(x0 + xoff(rng)));
                int y = static_cast<int>(std::round(y0 + yoff(rng)));
                points.emplace_back(SDL_Point{ std::clamp(x, 0, map_width), std::clamp(y, 0, map_height) });
        }
        bounds_valid_ = false;
        apply_resolution_to_points();
}

void Area::contract(int inset) {
        if (inset <= 0) return;
        for (auto& p : points) {
                if (p.x > inset) p.x -= inset;
                if (p.y > inset) p.y -= inset;
        }
        bounds_valid_ = false;
        apply_resolution_to_points();
        update_geometry_data();
}

double Area::get_area() const {
	return area_size;
}

const std::vector<Area::Point>& Area::get_points() const {
	return points;
}

void Area::union_with(const Area& other) {
        points.insert(points.end(), other.points.begin(), other.points.end());
        bounds_valid_ = false;
        apply_resolution_to_points();
        update_geometry_data();
}

bool Area::contains_point(const Point& pt) const {
	const size_t n = points.size();
        if (n == 1) {
                return pt.x == points[0].x && pt.y == points[0].y;
        }
	if (n < 3) return false;
	auto [minx, miny, maxx, maxy] = get_bounds();
        if (pt.x < minx || pt.x > maxx || pt.y < miny || pt.y > maxy) {
                return false;
        }
        bool inside = false;
        const double x = pt.x;
        const double y = pt.y;
        for (size_t i = 0, j = n - 1; i < n; j = i++) {
                const double xi = points[i].x;
                const double yi = points[i].y;
                const double xj = points[j].x;
                const double yj = points[j].y;
                const bool intersect = ((yi > y) != (yj > y)) && (x < (xj - xi) * (y - yi) / (yj - yi + 1e-12) + xi);
                if (intersect) inside = !inside;
        }
        return inside;
}

bool Area::intersects(const Area& other) const {
	auto [a0, a1, a2, a3] = get_bounds();
	auto [b0, b1, b2, b3] = other.get_bounds();
	return !(a2 < b0 || b2 < a0 || a3 < b1 || b3 < a1);
}

void Area::update_geometry_data() {
	if (points.empty()) {
		center_x = 0;
		center_y = 0;
		area_size = 0.0;
		min_x_ = min_y_ = max_x_ = max_y_ = 0;
		bounds_valid_ = true;
		return;
	}
        int minx = points[0].x, maxx = minx;
        int miny = points[0].y, maxy = miny;
        long long twice_area = 0;
        const size_t n = points.size();
        for (size_t i = 0, j = n - 1; i < n; j = i++) {
                const int xi = points[i].x;
                const int yi = points[i].y;
                const int xj = points[j].x;
                const int yj = points[j].y;
                minx = std::min(minx, xi);
                maxx = std::max(maxx, xi);
                miny = std::min(miny, yi);
                maxy = std::max(maxy, yi);
                twice_area += static_cast<long long>(xj) * yi - static_cast<long long>(xi) * yj;
        }
	min_x_ = minx; min_y_ = miny; max_x_ = maxx; max_y_ = maxy;
	bounds_valid_ = true;
	center_x = (minx + maxx) / 2;
	center_y = (miny + maxy) / 2;
	area_size = std::abs(static_cast<double>(twice_area)) * 0.5;
}

Area::Point Area::random_point_within() const {
        if (points.size() == 1) {
                return points[0];
        }
        auto [minx, miny, maxx, maxy] = get_bounds();
        for (int i = 0; i < 100; ++i) {
                int x = std::uniform_int_distribution<int>(minx, maxx)(rng);
                int y = std::uniform_int_distribution<int>(miny, maxy)(rng);
                if (contains_point(SDL_Point{ x, y })) return SDL_Point{ x, y };
        }
        return SDL_Point{0, 0};
}

Area::Point Area::get_center() const {
        return SDL_Point{ center_x, center_y };
}

double Area::get_size() const {
	return area_size;
}

SDL_Texture* Area::get_texture() const {
	return texture_;
}

void Area::set_cached_texture(SDL_Texture* texture) {
	if (!texture) return;
	texture_ = texture;
	SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_BLEND);
}

void Area::create_area_texture(SDL_Renderer* renderer) {
	if (!renderer || points.size() < 3) return;
	auto [minx, miny, maxx, maxy] = get_bounds();
	int w = maxx - minx + 1;
	int h = maxy - miny + 1;
	SDL_Texture* target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, w, h);
	if (!target) return;
	SDL_Texture* prev_target = SDL_GetRenderTarget(renderer);
	SDL_SetRenderTarget(renderer, target);
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
	SDL_RenderClear(renderer);
	SDL_SetRenderDrawColor(renderer, 0, 255, 0, 100);
        std::vector<SDL_Point> line_points;
        line_points.reserve(points.size() + 1);
        for (const auto& p : points) {
                line_points.push_back(SDL_Point{ p.x - minx, p.y - miny });
        }
        if (!line_points.empty()) {
                line_points.push_back(line_points.front());
                SDL_RenderDrawLines(renderer, line_points.data(), static_cast<int>(line_points.size()));
        }
	SDL_SetRenderTarget(renderer, prev_target);
	texture_ = target;
	SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_BLEND);
}

void Area::flip_horizontal(std::optional<int> axis_x) {
        if (points.empty()) return;
        int cx = axis_x.has_value() ? *axis_x : center_x;
        for (auto& p : points) {
                p.x = 2 * cx - p.x;
        }
        pos.x = 2 * cx - pos.x;
        bounds_valid_ = false;
        apply_resolution_to_points();
        update_geometry_data();
}

void Area::scale(float factor) {
        if (points.empty() || factor <= 0.0f) return;
        const int pivot_x = center_x;
        const int pivot_y = center_y;
        for (auto& p : points) {
                const float dx = static_cast<float>(p.x  - pivot_x);
                const float dy = static_cast<float>(p.y - pivot_y);
                p.x  = pivot_x + static_cast<int>(std::lround(dx * factor));
                p.y = pivot_y + static_cast<int>(std::lround(dy * factor));
        }
        bounds_valid_ = false;
        apply_resolution_to_points();
        auto [minx, miny, maxx, maxy] = get_bounds();
        pos.x = (minx + maxx) / 2;
        pos.y = maxy;
        update_geometry_data();
}

bool Area::apply_resolution_to_points() {
        const int clamped = vibble::grid::clamp_resolution(resolution_);
        resolution_ = clamped;
        bool changed = false;
        for (auto& p : points) {
                SDL_Point snapped = vibble::grid::snap_world_to_vertex(p, clamped);
                if (snapped.x != p.x || snapped.y != p.y) {
                        p = snapped;
                        changed = true;
                }
        }
        SDL_Point snapped_pos = vibble::grid::snap_world_to_vertex(pos, clamped);
        if (snapped_pos.x != pos.x || snapped_pos.y != pos.y) {
                pos = snapped_pos;
                changed = true;
        }
        if (changed) {
                bounds_valid_ = false;
        }
        return changed;
}

void Area::set_resolution(int r) {
        const int clamped = vibble::grid::clamp_resolution(r);
        if (resolution_ == clamped) {
                if (apply_resolution_to_points()) {
                        update_geometry_data();
                }
                return;
        }
        resolution_ = clamped;
        if (apply_resolution_to_points()) {
                update_geometry_data();
        }
}

