#include "range_util.hpp"

#include <cmath>
#include <limits>
#include "asset/Asset.hpp"

bool Range::xy(const Asset* a, double& x, double& y) {
    x = 0.0;
    y = 0.0;
    if (!a) return false;
    x = static_cast<double>(a->pos.x);
    y = static_cast<double>(a->pos.y);
    return true;
}

bool Range::xy(const SDL_Point& p, double& x, double& y) {
    x = static_cast<double>(p.x);
    y = static_cast<double>(p.y);
    return true;
}

bool Range::in_range_xy(double ax, double ay, double bx, double by, int radius) {
    double dx = ax - bx;
    double dy = ay - by;
    const double r2 = static_cast<double>(radius) * static_cast<double>(radius);
    const double d2 = dx * dx + dy * dy;
    return d2 <= r2;
}

double Range::distance_xy(double ax, double ay, double bx, double by) {
    double dx = ax - bx;
    double dy = ay - by;
    return std::sqrt(dx * dx + dy * dy);
}

bool Range::is_in_range(const Asset* a, const Asset* b, int radius) {
    double ax, ay, bx, by;
    if (!xy(a, ax, ay) || !xy(b, bx, by)) return false;
    return in_range_xy(ax, ay, bx, by, radius);
}

bool Range::is_in_range(const Asset* a, const SDL_Point& b, int radius) {
    double ax, ay, bx, by;
    if (!xy(a, ax, ay) || !xy(b, bx, by)) return false;
    return in_range_xy(ax, ay, bx, by, radius);
}

bool Range::is_in_range(const SDL_Point& a, const Asset* b, int radius) {
    double ax, ay, bx, by;
    if (!xy(a, ax, ay) || !xy(b, bx, by)) return false;
    return in_range_xy(ax, ay, bx, by, radius);
}

bool Range::is_in_range(const SDL_Point& a, const SDL_Point& b, int radius) {
    double ax, ay, bx, by;
    xy(a, ax, ay);
    xy(b, bx, by);
    return in_range_xy(ax, ay, bx, by, radius);
}

double Range::get_distance(const Asset* a, const Asset* b) {
    double ax, ay, bx, by;
    if (!xy(a, ax, ay) || !xy(b, bx, by)) return std::numeric_limits<double>::infinity();
    return distance_xy(ax, ay, bx, by);
}

double Range::get_distance(const Asset* a, const SDL_Point& b) {
    double ax, ay, bx, by;
    if (!xy(a, ax, ay) || !xy(b, bx, by)) return std::numeric_limits<double>::infinity();
    return distance_xy(ax, ay, bx, by);
}

double Range::get_distance(const SDL_Point& a, const Asset* b) {
    double ax, ay, bx, by;
    if (!xy(a, ax, ay) || !xy(b, bx, by)) return std::numeric_limits<double>::infinity();
    return distance_xy(ax, ay, bx, by);
}

double Range::get_distance(const SDL_Point& a, const SDL_Point& b) {
    double ax, ay, bx, by;
    xy(a, ax, ay);
    xy(b, bx, by);
    return distance_xy(ax, ay, bx, by);
}

void Range::get_in_range(const SDL_Point& center,
                         int radius,
                         const std::vector<Asset*>& candidates,
                         std::vector<Asset*>& out) {
    double cx, cy;
    out.clear();
    xy(center, cx, cy);
    const double r2 = static_cast<double>(radius) * static_cast<double>(radius);
    for (Asset* a : candidates) {
        if (!a) continue;
        double ax, ay;
        if (!xy(a, ax, ay)) continue;
        double dx = ax - cx;
        double dy = ay - cy;
        double d2 = dx * dx + dy * dy;
        if (d2 <= r2) out.push_back(a);
    }
}
