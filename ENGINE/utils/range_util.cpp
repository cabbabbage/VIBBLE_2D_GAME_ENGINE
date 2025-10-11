#include "range_util.hpp"

#include <cmath>
#include <limits>

#include "asset/Asset.hpp"

namespace {
bool is_within_radius(long long ax, long long ay, long long bx, long long by, int radius) {
    const long long dx = ax - bx;
    const long long dy = ay - by;
    const long long r = static_cast<long long>(radius);
    return dx * dx + dy * dy <= r * r;
}

long long distance_squared(long long ax, long long ay, long long bx, long long by) {
    const long long dx = ax - bx;
    const long long dy = ay - by;
    return dx * dx + dy * dy;
}

bool resolve_asset_pos(const Asset* asset, long long& x, long long& y) {
    if (!asset) {
        return false;
    }
    x = static_cast<long long>(asset->pos.x);
    y = static_cast<long long>(asset->pos.y);
    return true;
}
}

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
    long long ax, ay, bx, by;
    if (!resolve_asset_pos(a, ax, ay) || !resolve_asset_pos(b, bx, by)) {
        return false;
    }
    return is_within_radius(ax, ay, bx, by, radius);
}

bool Range::is_in_range(const Asset* a, const SDL_Point& b, int radius) {
    long long ax, ay;
    if (!resolve_asset_pos(a, ax, ay)) {
        return false;
    }
    return is_within_radius(ax, ay, static_cast<long long>(b.x), static_cast<long long>(b.y), radius);
}

bool Range::is_in_range(const SDL_Point& a, const Asset* b, int radius) {
    long long bx, by;
    if (!resolve_asset_pos(b, bx, by)) {
        return false;
    }
    return is_within_radius(static_cast<long long>(a.x), static_cast<long long>(a.y), bx, by, radius);
}

bool Range::is_in_range(const SDL_Point& a, const SDL_Point& b, int radius) {
    return is_within_radius(static_cast<long long>(a.x), static_cast<long long>(a.y), static_cast<long long>(b.x), static_cast<long long>(b.y), radius);
}

long long Range::distance_sq(const Asset* a, const Asset* b) {
    long long ax, ay, bx, by;
    if (!resolve_asset_pos(a, ax, ay) || !resolve_asset_pos(b, bx, by)) {
        return std::numeric_limits<long long>::max();
    }
    return distance_squared(ax, ay, bx, by);
}

long long Range::distance_sq(const Asset* a, const SDL_Point& b) {
    long long ax, ay;
    if (!resolve_asset_pos(a, ax, ay)) {
        return std::numeric_limits<long long>::max();
    }
    return distance_squared(ax, ay, static_cast<long long>(b.x), static_cast<long long>(b.y));
}

long long Range::distance_sq(const SDL_Point& a, const Asset* b) {
    long long bx, by;
    if (!resolve_asset_pos(b, bx, by)) {
        return std::numeric_limits<long long>::max();
    }
    return distance_squared(static_cast<long long>(a.x), static_cast<long long>(a.y), bx, by);
}

long long Range::distance_sq(const SDL_Point& a, const SDL_Point& b) {
    return distance_squared(static_cast<long long>(a.x), static_cast<long long>(a.y), static_cast<long long>(b.x), static_cast<long long>(b.y));
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
    out.clear();
    const long long cx = static_cast<long long>(center.x);
    const long long cy = static_cast<long long>(center.y);
    const long long r = static_cast<long long>(radius);
    const long long r2 = r * r;
    for (Asset* a : candidates) {
        if (!a) continue;
        const long long ax = static_cast<long long>(a->pos.x);
        const long long ay = static_cast<long long>(a->pos.y);
        const long long dx = ax - cx;
        const long long dy = ay - cy;
        if (dx * dx + dy * dy <= r2) {
            out.push_back(a);
        }
    }
}
