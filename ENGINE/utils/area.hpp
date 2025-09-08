#pragma once

#include <vector>
#include <string>
#include <tuple>
#include <optional>
#include <utility>
#include <SDL.h>
#include "parallax.hpp"

class Area {

	public:
    using Point = std::pair<int, int>;

	public:
    int pos_X = 0;
    int pos_Y = 0;
    void apply_parallax(const Parallax& parallax);

	public:
    explicit Area(const std::string& name);
    Area(const std::string& name, const std::vector<Point>& pts);
    Area(const std::string& name, int cx, int cy, int w, int h, const std::string& geometry, int edge_smoothness, int map_width, int map_height);
    Area(const std::string& name, const std::string& json_path, float scale);
    Area(const std::string& name, const Area& base, SDL_Renderer* renderer, int window_w = 0, int window_h = 0);
    Area(const std::string& name, SDL_Texture* background, SDL_Renderer* renderer, int window_w = 0, int window_h = 0);

	public:
    void apply_offset(int dx, int dy);
    void align(int target_x, int target_y);
    std::tuple<int, int, int, int> get_bounds() const;
    void generate_circle(int cx, int cy, int radius, int edge_smoothness, int map_width, int map_height);
    void generate_square(int cx, int cy, int w, int h, int edge_smoothness, int map_width, int map_height);
    void generate_point(int cx, int cy, int map_width, int map_height);
    void contract(int inset);
    double get_area() const;
    const std::vector<Point>& get_points() const;
    void union_with(const Area& other);
    bool contains_point(const Point& pt) const;
    bool intersects(const Area& other) const;
    void update_geometry_data();
    Point random_point_within() const;
    Point get_center() const;
    double get_size() const;

	public:
    const std::string& get_name() const { return area_name_; }
    SDL_Texture* get_texture() const;
    void create_area_texture(SDL_Renderer* renderer);

	public:
    void flip_horizontal(std::optional<int> axis_x = std::nullopt);
    void scale(float factor);

	private:
    std::vector<Point> points;
    std::string area_name_;
    int center_x = 0;
    int center_y = 0;
    double area_size = 0.0;
    SDL_Texture* texture_ = nullptr;
    mutable int min_x_ = 0;
    mutable int min_y_ = 0;
    mutable int max_x_ = 0;
    mutable int max_y_ = 0;
    mutable bool bounds_valid_ = false;
};
