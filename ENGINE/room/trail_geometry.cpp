#include "trail_geometry.hpp"
#include <cmath>
#include <random>
#include <vector>
#include <nlohmann/json.hpp>
#include "room.hpp"
#include "asset/asset_library.hpp"
#include "utils/area.hpp"
#include <iostream>
#include <limits>
using json = nlohmann::json;
std::vector<SDL_Point> TrailGeometry::build_centerline(const SDL_Point& start,
                                                       const SDL_Point& end,
                                                       int curvyness,
                                                       std::mt19937& rng)
{
	std::vector<SDL_Point> line;
	line.reserve(static_cast<size_t>(curvyness) + 2);
	line.push_back(start);
	if (curvyness > 0) {
		double dx = static_cast<double>(end.x - start.x);
		double dy = static_cast<double>(end.y - start.y);
		double len = std::hypot(dx, dy);
		if (len <= 0.0) len = 1.0;
		double max_offset = len * 0.25 * (static_cast<double>(curvyness) / 8.0);
		std::uniform_real_distribution<double> offset_dist(-max_offset, max_offset);
		for (int i = 1; i <= curvyness; ++i) {
			double t  = static_cast<double>(i) / (curvyness + 1);
			double px = start.x + t * dx;
			double py = start.y + t * dy;
			double nx = -dy / len;
			double ny =  dx / len;
			double off = offset_dist(rng);
			line.push_back(SDL_Point{
				static_cast<int>(std::lround(px + nx * off)),
				static_cast<int>(std::lround(py + ny * off))
			});
		}
	}
	line.push_back(end);
	return line;
}

std::vector<SDL_Point> TrailGeometry::extrude_centerline(const std::vector<SDL_Point>& centerline,
                                                         double width)
{
	const double half_w = width * 0.5;
	std::vector<SDL_Point> left, right;
	left.reserve(centerline.size());
	right.reserve(centerline.size());
	for (size_t i = 0; i < centerline.size(); ++i) {
		double cx = static_cast<double>(centerline[i].x);
		double cy = static_cast<double>(centerline[i].y);
		double dx, dy;
		if (i == 0) {
			dx = static_cast<double>(centerline[i + 1].x - centerline[i].x);
			dy = static_cast<double>(centerline[i + 1].y - centerline[i].y);
		} else if (i == centerline.size() - 1) {
			dx = static_cast<double>(centerline[i].x - centerline[i - 1].x);
			dy = static_cast<double>(centerline[i].y - centerline[i - 1].y);
		} else {
			dx = static_cast<double>(centerline[i + 1].x - centerline[i - 1].x);
			dy = static_cast<double>(centerline[i + 1].y - centerline[i - 1].y);
		}
		double len = std::hypot(dx, dy);
		if (len <= 0.0) len = 1.0;
		double nx = -dy / len;
		double ny =  dx / len;
		left.push_back(SDL_Point{
			static_cast<int>(std::lround(cx + nx * half_w)),
			static_cast<int>(std::lround(cy + ny * half_w))
		});
		right.push_back(SDL_Point{
			static_cast<int>(std::lround(cx - nx * half_w)),
			static_cast<int>(std::lround(cy - ny * half_w))
		});
	}
	std::vector<SDL_Point> polygon;
	polygon.reserve(left.size() + right.size());
	polygon.insert(polygon.end(), left.begin(), left.end());
	polygon.insert(polygon.end(), right.rbegin(), right.rend());
	return polygon;
}

SDL_Point TrailGeometry::compute_edge_point(const SDL_Point& center,
                                            const SDL_Point& toward,
                                            const Area* area)
{
	if (!area) return center;
	double dx = static_cast<double>(toward.x - center.x);
	double dy = static_cast<double>(toward.y - center.y);
	double len = std::hypot(dx, dy);
	if (len <= 0.0) return center;
	double dirX = dx / len;
	double dirY = dy / len;
	const int max_steps = 2000;
	const double step_size = 1.0;
	SDL_Point edge = center;
	for (int i = 1; i <= max_steps; ++i) {
		double px = center.x + dirX * i * step_size;
		double py = center.y + dirY * i * step_size;
		int ipx = static_cast<int>(std::lround(px));
		int ipy = static_cast<int>(std::lround(py));
		if (area->contains_point(SDL_Point{ ipx, ipy })) {
			edge = SDL_Point{ ipx, ipy };
		} else {
			break;
		}
	}
	return edge;
}

bool TrailGeometry::attempt_trail_connection(Room* a,
                                             Room* b,
                                             std::vector<Area>& existing_areas,
                                             const std::string& map_dir,
                                             const std::string& map_info_path,
                                             AssetLibrary* asset_lib,
                                             std::vector<std::unique_ptr<Room>>& trail_rooms,
                                             int allowed_intersections,
                                             nlohmann::json* trail_config,
                                             const std::string& trail_name,
                                             const nlohmann::json* map_assets_data,
                                             double map_radius,
                                             bool testing,
                                             std::mt19937& rng)
{
        if (!trail_config) {
                if (testing) {
                        std::cout << "[TrailGen] Missing trail configuration for '" << trail_name << "'\n";
                }
                return false;
        }
        json& config = *trail_config;
        const int min_width = config.value("min_width", 40);
        const int max_width = config.value("max_width", min_width);
        const int curvyness = config.value("curvyness", 2);
        const std::string name = config.value("name", trail_name.empty() ? std::string("trail_segment") : trail_name);
        const double width = static_cast<double>(std::max(min_width, max_width));
        if (testing) {
                std::cout << "[TrailGen] Using trail template: " << name
                << "  width=" << width
                << "  curvyness=" << curvyness << "\n";
        }
	const SDL_Point a_center = a->room_area->get_center();
	const SDL_Point b_center = b->room_area->get_center();
	const double overshoot = 100.0;
	const double min_interior_depth = std::max(40.0, width * 0.75);

	auto make_edge_triplet =
	[&](const SDL_Point& center, const SDL_Point& toward, const Area* area)
	-> std::tuple<SDL_Point, SDL_Point, SDL_Point>
	{
		SDL_Point edge = TrailGeometry::compute_edge_point(center, toward, area);
		double dx = static_cast<double>(edge.x - center.x);
		double dy = static_cast<double>(edge.y - center.y);
		double len = std::hypot(dx, dy);
		if (len <= 0.0) len = 1.0;
		double ux = dx / len;
		double uy = dy / len;
		SDL_Point outside{
			static_cast<int>(std::lround(edge.x + ux * overshoot)),
			static_cast<int>(std::lround(edge.y + uy * overshoot))
		};
		SDL_Point interior{
			static_cast<int>(std::lround(edge.x - ux * min_interior_depth)),
			static_cast<int>(std::lround(edge.y - uy * min_interior_depth))
		};
		auto is_inside = [&](const SDL_Point& p)->bool{
			return area->contains_point(p);
		};
		if (!is_inside(interior)) {
			const int max_fix_steps = 1024;
			const double step = 2.0;
			double px = static_cast<double>(interior.x);
			double py = static_cast<double>(interior.y);
			for (int i = 0; i < max_fix_steps; ++i) {
				SDL_Point test{ static_cast<int>(std::lround(px)),
				                static_cast<int>(std::lround(py)) };
				if (is_inside(test)) { interior = test; break; }
				px -= ux * step;
				py -= uy * step;
				if (std::hypot(px - center.x, py - center.y) > len + 2.0) {
					break;
				}
			}
			if (!is_inside(interior)) {
				interior = center;
			}
		}
		return std::make_tuple(interior, edge, outside);
	};

	SDL_Point a_interior, a_edge, a_outside;
	std::tie(a_interior, a_edge, a_outside) =
	    make_edge_triplet(a_center, b_center, a->room_area.get());

	SDL_Point b_interior, b_edge, b_outside;
	std::tie(b_interior, b_edge, b_outside) =
	    make_edge_triplet(b_center, a_center, b->room_area.get());

	auto [aminx, aminy, amaxx, amaxy] = a->room_area->get_bounds();
	auto [bminx, bminy, bmaxx, bmaxy] = b->room_area->get_bounds();

	for (int attempt = 0; attempt < 1000; ++attempt) {
		std::vector<SDL_Point> full_line;
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
			pts.push_back(SDL_Point{ p.x, p.y });
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

                auto trail_room = std::make_unique<Room>(
                        a->map_origin,
                        "trail",
                        name,
                        nullptr,
                        map_dir,
                        map_info_path,
                        asset_lib,
                        &candidate,
                        trail_config,
                        map_assets_data,
                        map_radius,
                        "trails_data"
                );
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
