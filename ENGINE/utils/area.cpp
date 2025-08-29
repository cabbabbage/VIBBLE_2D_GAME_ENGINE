
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
#include "parallax.hpp"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static std::mt19937 rng{std::random_device{}()};

// --- Helpers for the interactive editor ---
namespace {
struct EditorResult {
    std::vector<Area::Point> points; // in background pixel coordinates (top-left origin)
    int bg_w = 0;
    int bg_h = 0;
};

static void stamp_circle(SDL_Surface* surf, int cx, int cy, int r, Uint32 color) {
    if (!surf) return;
    SDL_LockSurface(surf);
    Uint8* pixels = static_cast<Uint8*>(surf->pixels);
    int pitch = surf->pitch;
    for (int y = -r; y <= r; ++y) {
        int yy = cy + y;
        if (yy < 0 || yy >= surf->h) continue;
        int xr = static_cast<int>(std::sqrt(static_cast<double>(r * r - y * y)));
        for (int x = -xr; x <= xr; ++x) {
            int xx = cx + x;
            if (xx < 0 || xx >= surf->w) continue;
            Uint32* p = reinterpret_cast<Uint32*>(pixels + yy * pitch) + xx;
            *p = color;
        }
    }
    SDL_UnlockSurface(surf);
}

static std::vector<Area::Point> extract_edge_points(SDL_Surface* surf, int step = 1) {
    std::vector<Area::Point> out;
    if (!surf) return out;
    SDL_LockSurface(surf);
    const Uint8* pixels = static_cast<const Uint8*>(surf->pixels);
    const int pitch = surf->pitch;
    auto getA = [&](int x, int y) -> Uint8 {
        const Uint32* p = reinterpret_cast<const Uint32*>(pixels + y * pitch) + x;
        Uint32 v = *p;
        return static_cast<Uint8>((v >> 24) & 0xFF);
    };
    for (int y = 1; y < surf->h - 1; y += step) {
        for (int x = 1; x < surf->w - 1; x += step) {
            Uint8 a = getA(x, y);
            if (a == 0) continue;
            if (getA(x - 1, y) == 0 || getA(x + 1, y) == 0 || getA(x, y - 1) == 0 || getA(x, y + 1) == 0) {
                out.emplace_back(x, y);
            }
        }
    }
    SDL_UnlockSurface(surf);
    return out;
}

static EditorResult run_area_editor(SDL_Texture* background, int window_w, int window_h) {
    if (!background) throw std::runtime_error("[Area Editor] No background texture provided");

    int tex_w = 0, tex_h = 0;
    SDL_QueryTexture(background, nullptr, nullptr, &tex_w, &tex_h);
    if (tex_w <= 0 || tex_h <= 0) throw std::runtime_error("[Area Editor] Invalid background texture size");

    const int minW = 800, minH = 600;
    int win_w = window_w > 0 ? window_w : std::max(minW, tex_w);
    int win_h = window_h > 0 ? window_h : std::max(minH, tex_h);

    SDL_Window* win = SDL_CreateWindow("Area Editor", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                      win_w, win_h, SDL_WINDOW_SHOWN);
    if (!win) throw std::runtime_error("[Area Editor] Failed to create window");
    SDL_Renderer* rend = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!rend) {
        SDL_DestroyWindow(win);
        throw std::runtime_error("[Area Editor] Failed to create renderer");
    }

    // Prepare mask surface (ARGB): we'll use the alpha channel as the mask value
    SDL_Surface* mask = SDL_CreateRGBSurfaceWithFormat(0, tex_w, tex_h, 32, SDL_PIXELFORMAT_RGBA32);
    if (!mask) {
        SDL_DestroyRenderer(rend);
        SDL_DestroyWindow(win);
        throw std::runtime_error("[Area Editor] Failed to allocate mask surface");
    }
    // Clear mask to transparent
    SDL_FillRect(mask, nullptr, SDL_MapRGBA(mask->format, 255, 0, 0, 0));

    // Precompute scale/offset to fit background centered
    double scale = std::min(static_cast<double>(win_w) / tex_w, static_cast<double>(win_h) / tex_h);
    int draw_w = static_cast<int>(std::round(tex_w * scale));
    int draw_h = static_cast<int>(std::round(tex_h * scale));
    int off_x = (win_w - draw_w) / 2;
    int off_y = (win_h - draw_h) / 2;

    // Brush settings
    int brush = 10;
    bool drawing = false;
    bool erasing = false;

    bool quit = false;
    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { quit = true; }
            else if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_ESCAPE) { quit = true; }
                else if (e.key.keysym.sym == SDLK_RETURN) { quit = true; }
                else if (e.key.keysym.sym == SDLK_EQUALS || e.key.keysym.sym == SDLK_PLUS) { brush = std::min(300, brush + 2); }
                else if (e.key.keysym.sym == SDLK_MINUS) { brush = std::max(1, brush - 2); }
                else if (e.key.keysym.sym == SDLK_c) { SDL_FillRect(mask, nullptr, SDL_MapRGBA(mask->format, 255, 0, 0, 0)); }
            } else if (e.type == SDL_MOUSEBUTTONDOWN) {
                if (e.button.button == SDL_BUTTON_LEFT) drawing = true;
                if (e.button.button == SDL_BUTTON_RIGHT) erasing = true;
            } else if (e.type == SDL_MOUSEBUTTONUP) {
                if (e.button.button == SDL_BUTTON_LEFT) drawing = false;
                if (e.button.button == SDL_BUTTON_RIGHT) erasing = false;
            } else if (e.type == SDL_MOUSEMOTION) {
                int mx = e.motion.x;
                int my = e.motion.y;
                // Map to texture space
                int tx = static_cast<int>(std::round((mx - off_x) / scale));
                int ty = static_cast<int>(std::round((my - off_y) / scale));
                if (tx >= 0 && tx < tex_w && ty >= 0 && ty < tex_h) {
                    if (drawing) {
                        stamp_circle(mask, tx, ty, brush, SDL_MapRGBA(mask->format, 255, 0, 0, 255));
                    } else if (erasing) {
                        stamp_circle(mask, tx, ty, brush, SDL_MapRGBA(mask->format, 255, 0, 0, 0));
                    }
                }
            }
        }

        SDL_SetRenderDrawColor(rend, 20, 20, 20, 255);
        SDL_RenderClear(rend);
        // Draw background centered
        SDL_Rect dst{ off_x, off_y, draw_w, draw_h };
        SDL_RenderCopy(rend, background, nullptr, &dst);

        // Draw red overlay from mask
        SDL_Texture* overlay = SDL_CreateTextureFromSurface(rend, mask);
        if (overlay) {
            SDL_SetTextureBlendMode(overlay, SDL_BLENDMODE_BLEND);
            SDL_SetTextureAlphaMod(overlay, 128);
            SDL_RenderCopy(rend, overlay, nullptr, &dst);
            SDL_DestroyTexture(overlay);
        }

        // Draw brush preview
        int mx, my; Uint32 mstate = SDL_GetMouseState(&mx, &my);
        SDL_SetRenderDrawColor(rend, 255, 255, 255, 255);
        // Approximate circle preview
        for (int a = 0; a < 360; a += 6) {
            double rad = a * M_PI / 180.0;
            int x = mx + static_cast<int>(std::cos(rad) * brush);
            int y = my + static_cast<int>(std::sin(rad) * brush);
            SDL_RenderDrawPoint(rend, x, y);
        }

        SDL_RenderPresent(rend);
    }

    EditorResult res;
    res.bg_w = tex_w;
    res.bg_h = tex_h;
    res.points = extract_edge_points(mask, /*step=*/1);

    SDL_FreeSurface(mask);
    SDL_DestroyRenderer(rend);
    SDL_DestroyWindow(win);
    return res;
}
} // namespace

Area::Area(const std::string& name)
    : pos_X(0), pos_Y(0), area_name_(name) {}

Area::Area(const std::string& name, const std::vector<Point>& pts)
    : points(pts), area_name_(name)
{
    if (!points.empty()) {
        auto [minx, miny, maxx, maxy] = get_bounds();
        pos_X = (minx + maxx) / 2;
        pos_Y = maxy;
        update_geometry_data();
    }
}

Area::Area(const std::string& name, int cx, int cy, int w, int h,
           const std::string& geometry,
           int edge_smoothness,
           int map_width, int map_height)
    : area_name_(name)
{
    if (w <= 0 || h <= 0 || map_width <= 0 || map_height <= 0) {
        throw std::runtime_error("[Area: " + area_name_ + "] Invalid dimensions");
    }
    if (geometry == "Circle") {
        generate_circle(cx, cy, w / 2, edge_smoothness, map_width, map_height);
    } else if (geometry == "Square") {
        generate_square(cx, cy, w, h, edge_smoothness, map_width, map_height);
    } else {
        throw std::runtime_error("[Area: " + area_name_ + "] Unknown geometry: " + geometry);
    }
    auto [minx, miny, maxx, maxy] = get_bounds();
    pos_X = (minx + maxx) / 2;
    pos_Y = maxy;
    update_geometry_data();
}

Area::Area(const std::string& name, const std::string& json_path, float scale)
    : area_name_(name)
{
    if (scale <= 0.0f)
        throw std::invalid_argument("[Area: " + area_name_ + "] Scale must be positive");

    std::ifstream in(json_path);
    if (!in.is_open())
        throw std::runtime_error("[Area: " + area_name_ + "] Failed to open JSON: " + json_path);

    nlohmann::json j;
    in >> j;

    auto& pts_json = j.at("points");
    auto& dim_json = j.at("original_dimensions");
    if (!pts_json.is_array() || !dim_json.is_array() || dim_json.size() != 2)
        throw std::runtime_error("[Area: " + area_name_ + "] Bad JSON: " + json_path);

    int orig_w = dim_json[0].get<int>();
    int orig_h = dim_json[1].get<int>();
    if (orig_w <= 0 || orig_h <= 0)
        throw std::runtime_error("[Area: " + area_name_ + "] Invalid dimensions in JSON");

    int pivot_x = static_cast<int>(std::round((orig_w / 2.0f) * scale));
    int pivot_y = static_cast<int>(std::round(orig_h * scale));

    points.clear();
    points.reserve(pts_json.size());

    for (auto& elem : pts_json) {
        if (!elem.is_array() || elem.size() < 2) continue;
        float rel_x = elem[0].get<float>();
        float rel_y = elem[1].get<float>();
        int x = pivot_x + static_cast<int>(std::round(rel_x * scale));
        int y = pivot_y + static_cast<int>(std::round(rel_y * scale));
        points.emplace_back(x, y);
    }

    if (points.empty())
        throw std::runtime_error("[Area: " + area_name_ + "] No points loaded");

    pos_X = pivot_x;
    pos_Y = pivot_y;

    int dx = j.value("offset_x", 0);
    int dy = -j.value("offset_y", 0);

    if (dx != 0 || dy != 0) {
        apply_offset(dx, dy);
    }

    update_geometry_data();
}

Area::Area(const std::string& name, const Area& base, SDL_Renderer* renderer,
           int window_w, int window_h)
    : area_name_(name)
{
    SDL_Texture* bg = base.get_texture();
    if (!bg && renderer) {
        const_cast<Area&>(base).create_area_texture(renderer);
        bg = base.get_texture();
    }
    if (!bg) throw std::runtime_error("[Area: editor] Base area has no background texture");

    EditorResult er = run_area_editor(bg, window_w, window_h);
    if (er.points.empty()) {
        throw std::runtime_error("[Area: editor] No points drawn");
    }

    // Construct absolute points in image pixel space; pivot is bottom-center
    int pivot_x = er.bg_w / 2;
    int pivot_y = er.bg_h;

    points.clear();
    points.reserve(er.points.size());
    for (const auto& p : er.points) {
        points.emplace_back(p.first, p.second);
    }
    pos_X = pivot_x;
    pos_Y = pivot_y;
    update_geometry_data();
}

Area::Area(const std::string& name, SDL_Texture* background, SDL_Renderer* /*renderer*/,
           int window_w, int window_h)
    : area_name_(name)
{
    if (!background) throw std::runtime_error("[Area: editor] Null background texture");
    EditorResult er = run_area_editor(background, window_w, window_h);
    if (er.points.empty()) {
        throw std::runtime_error("[Area: editor] No points drawn");
    }
    int pivot_x = er.bg_w / 2;
    int pivot_y = er.bg_h;
    points.clear();
    points.reserve(er.points.size());
    for (const auto& p : er.points) {
        points.emplace_back(p.first, p.second);
    }
    pos_X = pivot_x;
    pos_Y = pivot_y;
    update_geometry_data();
}

void Area::apply_offset(int dx, int dy) {
    for (auto& p : points) {
        p.first += dx;
        p.second += dy;
    }
    pos_X += dx;
    pos_Y += dy;
    update_geometry_data();
}

void Area::align(int target_x, int target_y) {
    int dx = target_x - pos_X;
    int dy = target_y - pos_Y;
    apply_offset(dx, dy);
}

std::tuple<int, int, int, int> Area::get_bounds() const {
    if (bounds_valid_) {
        return {min_x_, min_y_, max_x_, max_y_};
    }
    if (points.empty())
        throw std::runtime_error("[Area: " + area_name_ + "] get_bounds() on empty point set");

    int minx = points[0].first, maxx = minx;
    int miny = points[0].second, maxy = miny;
    for (const auto& p : points) {
        minx = std::min(minx, p.first);
        maxx = std::max(maxx, p.first);
        miny = std::min(miny, p.second);
        maxy = std::max(maxy, p.second);
    }
    min_x_ = minx; min_y_ = miny; max_x_ = maxx; max_y_ = maxy;
    bounds_valid_ = true;
    return {minx, miny, maxx, maxy};
}

void Area::generate_circle(int cx, int cy, int radius, int edge_smoothness, int map_width, int map_height) {
    int s = std::clamp(edge_smoothness, 0, 100);
    int count = std::max(12, 6 + s * 2);
    double max_dev = 0.20 * (100 - s) / 100.0;
    std::uniform_real_distribution<double> dist(1.0 - max_dev, 1.0 + max_dev);

    points.clear();
    points.reserve(count);
    for (int i = 0; i < count; ++i) {
        double theta = 2 * M_PI * i / count;
        double rx = radius * dist(rng), ry = radius * dist(rng);
        double x = cx + rx * std::cos(theta);
        double y = cy + ry * std::sin(theta);
        int xi = static_cast<int>(std::round(std::clamp(x, 0.0, static_cast<double>(map_width))));
        int yi = static_cast<int>(std::round(std::clamp(y, 0.0, static_cast<double>(map_height))));
        points.emplace_back(xi, yi);
    }
}

void Area::generate_square(int cx, int cy, int w, int h, int edge_smoothness, int map_width, int map_height) {
    int s = std::clamp(edge_smoothness, 0, 100);
    double max_dev = 0.25 * (100 - s) / 100.0;
    std::uniform_real_distribution<double> xoff(-max_dev * w, max_dev * w);
    std::uniform_real_distribution<double> yoff(-max_dev * h, max_dev * h);
    int half_w = w / 2, half_h = h / 2;

    points.clear();
    points.reserve(4);
    for (auto [x0, y0] : std::array<Point, 4>{
             Point{cx - half_w, cy - half_h},
             Point{cx + half_w, cy - half_h},
             Point{cx + half_w, cy + half_h},
             Point{cx - half_w, cy + half_h}}) {
        int x = static_cast<int>(std::round(x0 + xoff(rng)));
        int y = static_cast<int>(std::round(y0 + yoff(rng)));
        points.emplace_back(std::clamp(x, 0, map_width), std::clamp(y, 0, map_height));
    }
}

void Area::contract(int inset) {
    if (inset <= 0) return;
    for (auto& [x, y] : points) {
        if (x > inset) x -= inset;
        if (y > inset) y -= inset;
    }
    update_geometry_data();
}

double Area::get_area() const {
    // area_size is kept up-to-date in update_geometry_data()
    return area_size;
}

const std::vector<Area::Point>& Area::get_points() const {
    return points;
}

void Area::union_with(const Area& other) {
    points.insert(points.end(), other.points.begin(), other.points.end());
    update_geometry_data();
}

bool Area::contains_point(const Point& pt) const {
    const size_t n = points.size();
    if (n < 3) return false;

    // Fast AABB reject
    auto [minx, miny, maxx, maxy] = get_bounds();
    if (pt.first < minx || pt.first > maxx || pt.second < miny || pt.second > maxy) {
        return false;
    }

    bool inside = false;
    const double x = pt.first;
    const double y = pt.second;
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        const double xi = points[i].first;
        const double yi = points[i].second;
        const double xj = points[j].first;
        const double yj = points[j].second;

        const bool intersect = ((yi > y) != (yj > y)) &&
                               (x < (xj - xi) * (y - yi) / (yj - yi + 1e-12) + xi);
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

    // Compute bounds and polygon area in a single pass
    int minx = points[0].first, maxx = minx;
    int miny = points[0].second, maxy = miny;
    long long twice_area = 0;
    const size_t n = points.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        const int xi = points[i].first;
        const int yi = points[i].second;
        const int xj = points[j].first;
        const int yj = points[j].second;

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
    auto [minx, miny, maxx, maxy] = get_bounds();
    for (int i = 0; i < 100; ++i) {
        int x = std::uniform_int_distribution<int>(minx, maxx)(rng);
        int y = std::uniform_int_distribution<int>(miny, maxy)(rng);
        if (contains_point({x, y})) return {x, y};
    }
    return {0, 0};
}

Area::Point Area::get_center() const {
    return {center_x, center_y};
}

double Area::get_size() const {
    return area_size;
}

SDL_Texture* Area::get_texture() const {
    return texture_;
}

void Area::create_area_texture(SDL_Renderer* renderer) {
    if (!renderer || points.size() < 3) return;

    auto [minx, miny, maxx, maxy] = get_bounds();
    int w = maxx - minx + 1;
    int h = maxy - miny + 1;

    SDL_Texture* target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                            SDL_TEXTUREACCESS_TARGET, w, h);
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
        line_points.push_back(SDL_Point{ p.first - minx, p.second - miny });
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
        p.first = 2 * cx - p.first;
    }

    
    pos_X = 2 * cx - pos_X;

    update_geometry_data();
}



void Area::scale(float factor) {
    if (points.empty() || factor <= 0.0f) return;

    
    const int pivot_x = center_x;
    const int pivot_y = center_y;

    for (auto& p : points) {
        const float dx = static_cast<float>(p.first  - pivot_x);
        const float dy = static_cast<float>(p.second - pivot_y);
        p.first  = pivot_x + static_cast<int>(std::lround(dx * factor));
        p.second = pivot_y + static_cast<int>(std::lround(dy * factor));
    }

    
    auto [minx, miny, maxx, maxy] = get_bounds();
    pos_X = (minx + maxx) / 2;
    pos_Y = maxy;
    update_geometry_data();
}






void Area::apply_parallax(const Parallax& parallax) {
    for (auto& p : points) {
        SDL_Point scr = parallax.apply(p.first, p.second);
        p.first  = scr.x;
        p.second = scr.y;
    }

    
    update_geometry_data();
}

