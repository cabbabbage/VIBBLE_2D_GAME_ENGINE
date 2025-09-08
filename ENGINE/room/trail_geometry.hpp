#pragma once

#include <vector>
#include <string>
#include <memory>
#include <random>
#include <utility>

class Room;
class Area;
class AssetLibrary;

class TrailGeometry {
public:
    using Point = std::pair<double, double>;
    static std::vector<Point> build_centerline(
        const Point& start,
        const Point& end,
        int curvyness,
        std::mt19937& rng);
    static std::vector<Point> extrude_centerline(
        const std::vector<Point>& centerline,
        double width);
    static Point compute_edge_point(
        const Point& center,
        const Point& toward,
        const Area* area);
    static bool attempt_trail_connection(
        Room* a,
        Room* b,
        std::vector<Area>& existing_areas,
        const std::string& map_dir,
        AssetLibrary* asset_lib,
        std::vector<std::unique_ptr<Room>>& trail_rooms,
        int allowed_intersections,
        const std::string& path,
        bool testing,
        std::mt19937& rng);
};
