#include "trail_geometry.hpp"
#include <cmath>
#include <random>
#include <vector>
#include <fstream>
#include <nlohmann/json.hpp>
#include <filesystem>
#include "Room.hpp"
#include "asset\asset_library.hpp"
#include "utils/area.hpp"
#include <iostream>
#include <limits>
using json = nlohmann::json;
namespace fs = std::filesystem;

std::vector<TrailGeometry::Point> TrailGeometry::build_centerline(
                                                                      const Point& start, const Point& end, int curvyness, std::mt19937& rng)
{
	std::vector<Point> line;
	line.reserve(static_cast<size_t>(curvyness) + 2);
	line.push_back(start);
	if (curvyness > 0) {
		double dx = end.first  - start.first;
		double dy = end.second - start.second;
		double len = std::hypot(dx, dy);
		if (len <= 0.0) len = 1.0;
		double max_offset = len * 0.25 * (static_cast<double>(curvyness) / 8.0);
		std::uniform_real_distribution<double> offset_dist(-max_offset, max_offset);
		for (int i = 1; i <= curvyness; ++i) {
			double t  = static_cast<double>(i) / (curvyness + 1);
			double px = start.first  + t * dx;
			double py = start.second + t * dy;
			double nx = -dy / len;
			double ny =  dx / len;
			double off = offset_dist(rng);
			line.emplace_back( std::round(px + nx * off), std::round(py + ny * off) );
		}
	}
	line.push_back(end);
	return line;
}

std::vector<TrailGeometry::Point> TrailGeometry::extrude_centerline(
                                                                        const std::vector<Point>& centerline, double width)
{
	const double half_w = width * 0.5;
	std::vector<Point> left, right;
	left.reserve(centerline.size());
	right.reserve(centerline.size());
	for (size_t i = 0; i < centerline.size(); ++i) {
		double cx = centerline[i].first;
		double cy = centerline[i].second;
		double dx, dy;
		if (i == 0) {
			dx = centerline[i + 1].first  - cx;
			dy = centerline[i + 1].second - cy;
		} else if (i == centerline.size() - 1) {
			dx = cx - centerline[i - 1].first;
			dy = cy - centerline[i - 1].second;
		} else {
			dx = centerline[i + 1].first  - centerline[i - 1].first;
			dy = centerline[i + 1].second - centerline[i - 1].second;
		}
		double len = std::hypot(dx, dy);
		if (len <= 0.0) len = 1.0;
		double nx = -dy / len;
		double ny =  dx / len;
		left.emplace_back( std::round(cx + nx * half_w), std::round(cy + ny * half_w) );
		right.emplace_back( std::round(cx - nx * half_w), std::round(cy - ny * half_w) );
	}
	std::vector<Point> polygon;
	polygon.reserve(left.size() + right.size());
	polygon.insert(polygon.end(), left.begin(), left.end());
	polygon.insert(polygon.end(), right.rbegin(), right.rend());
	return polygon;
}

TrailGeometry::Point TrailGeometry::compute_edge_point(const Point& center,
                                                       const Point& toward,
                                                       const Area* area)
{
	if (!area) return center;
	double dx = toward.first  - center.first;
	double dy = toward.second - center.second;
	double len = std::hypot(dx, dy);
	if (len <= 0.0) return center;
	double dirX = dx / len;
	double dirY = dy / len;
	const int max_steps = 2000;
	const double step_size = 1.0;
	Point edge = center;
	for (int i = 1; i <= max_steps; ++i) {
		double px = center.first + dirX * i * step_size;
		double py = center.second + dirY * i * step_size;
		int ipx = static_cast<int>(std::round(px));
		int ipy = static_cast<int>(std::round(py));
		if (area->contains_point({ipx, ipy})) {
			edge = {px, py};
		} else {
			break;
		}
	}
	return edge;
}

bool TrailGeometry::attempt_trail_connection(
                                                 Room* a,
                                                 Room* b,
                                                 std::vector<Area>& existing_areas,
                                                 const std::string& map_dir,
                                                 AssetLibrary* asset_lib,
                                                 std::vector<std::unique_ptr<Room>>& trail_rooms,
                                                 int allowed_intersections,
                                                 const std::string& path,
                                                 bool testing,
                                                 std::mt19937& rng)
{
	std::ifstream in(path);
	if (!in.is_open()) {
		if (testing) std::cout << "[TrailGen] Failed to open asset: " << path << "\n";
		return false;
	}
	json config;
	in >> config;
	const int min_width = config.value("min_width", 40);
	const int max_width = config.value("max_width", 80);
	const int curvyness = config.value("curvyness", 2);
	const std::string name = config.value("name", "trail_segment");
	const double width = static_cast<double>( std::uniform_int_distribution<int>(min_width, max_width)(rng));
	if (testing) {
		std::cout << "[TrailGen] Using asset: " << path
		<< "  width=" << width
		<< "  curvyness=" << curvyness << "\n";
	}
	const Point a_center = a->room_area->get_center();
	const Point b_center = b->room_area->get_center();
	const double overshoot = 100.0;
	const double min_interior_depth = std::max(40.0, width * 0.75);
	auto make_edge_triplet = [&](const Point& center,
                              const Point& toward,
                              const Area* area)
	{
		Point edge = TrailGeometry::compute_edge_point(center, toward, area);
		double dx = edge.first  - center.first;
		double dy = edge.second - center.second;
		double len = std::hypot(dx, dy);
		if (len <= 0.0) len = 1.0;
		double ux = dx / len;
		double uy = dy / len;
		Point outside = { edge.first + ux * overshoot,
			edge.second + uy * overshoot };
		Point interior = { edge.first - ux * min_interior_depth,
			edge.second - uy * min_interior_depth };
		auto is_inside = [&](const Point& p)->bool{
			return area->contains_point({ static_cast<int>(std::round(p.first)),
					static_cast<int>(std::round(p.second)) });
		};
		if (!is_inside(interior)) {
			const int max_fix_steps = 1024;
			const double step = 2.0;
			Point p = interior;
			for (int i = 0; i < max_fix_steps; ++i) {
					if (is_inside(p)) { interior = p; break; }
					p.first  -= ux * step;
					p.second -= uy * step;
					if (std::hypot(p.first - center.first, p.second - center.second) > len + 2.0) {
								break;
					}
			}
			if (!is_inside(interior)) {
					interior = center;
			}
		}
		return std::make_tuple(interior, edge, outside);
	};
	Point a_interior, a_edge, a_outside;
	std::tie(a_interior, a_edge, a_outside) = make_edge_triplet(a_center, b_center, a->room_area.get());
	Point b_interior, b_edge, b_outside;
	std::tie(b_interior, b_edge, b_outside) = make_edge_triplet(b_center, a_center, b->room_area.get());
	auto [aminx, aminy, amaxx, amaxy] = a->room_area->get_bounds();
	auto [bminx, bminy, bmaxx, bmaxy] = b->room_area->get_bounds();
	for (int attempt = 0; attempt < 1000; ++attempt) {
		std::vector<Point> full_line;
		full_line.reserve(static_cast<size_t>(curvyness) + 6);
		full_line.push_back(a_interior);
		full_line.push_back(a_edge);
		auto middle = build_centerline(a_outside, b_outside, curvyness, rng);
		full_line.insert(full_line.end(), middle.begin(), middle.end());
		full_line.push_back(b_edge);
		full_line.push_back(b_interior);
		auto polygon = extrude_centerline(full_line, width);
		std::vector<Area::Point> pts;
		pts.reserve(polygon.size());
		for (auto& p : polygon) {
			pts.emplace_back(static_cast<int>(std::round(p.first)), static_cast<int>(std::round(p.second)));
		}
		Area candidate("trail_candidate", pts);
		int intersection_count = 0;
		for (auto& area : existing_areas) {
			auto [minx, miny, maxx, maxy] = area.get_bounds();
			bool isA = (minx == aminx && miny == aminy && maxx == amaxx && maxy == amaxy);
			bool isB = (minx == bminx && miny == bminy && maxx == bmaxx && maxy == bmaxy);
			if (isA || isB) continue;
			if (candidate.intersects(area)) {
					intersection_count++;
					break;
			}
		}
		if (intersection_count > allowed_intersections) {
			if (testing && attempt == 999) {
					std::cout << "[TrailGen] Failed after 1000 attempts due to intersections\n";
			}
			continue;
		}
		std::string room_dir = fs::path(path).parent_path().string();
		auto trail_room = std::make_unique<Room>( a->map_origin, "trail", name, nullptr, room_dir, map_dir, asset_lib, &candidate );
		a->add_connecting_room(trail_room.get());
		b->add_connecting_room(trail_room.get());
		trail_room->add_connecting_room(a);
		trail_room->add_connecting_room(b);
		existing_areas.push_back(candidate);
		trail_rooms.push_back(std::move(trail_room));
		if (testing) {
			std::cout << "[TrailGen] Trail succeeded on attempt " << attempt + 1 << "\n";
		}
		return true;
	}
	return false;
}
