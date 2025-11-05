#include "FloatingPanelLayoutManager.hpp"

#include <algorithm>
#include <numeric>
#include <vector>

#include "DockableCollapsible.hpp"

namespace {
constexpr int kPanelGap = 40;
constexpr int kHeaderToPanelPadding = 30;

struct Interval {
    int start = 0;
    int end = 0;
};

bool has_area(const SDL_Rect& rect) {
    return rect.w > 0 && rect.h > 0;
}

SDL_Rect clamp_rect_to_view(const SDL_Rect& rect) {
    SDL_Rect result = rect;
    if (result.w < 0) {
        result.x += result.w;
        result.w = -result.w;
    }
    if (result.h < 0) {
        result.y += result.h;
        result.h = -result.h;
    }
    return result;
}

std::vector<Interval> subtract_interval(const std::vector<Interval>& source, const Interval& obstacle) {
    std::vector<Interval> result;
    result.reserve(source.size() + 1);
    for (const Interval& interval : source) {
        if (obstacle.end <= interval.start || obstacle.start >= interval.end) {
            result.push_back(interval);
            continue;
        }
        if (obstacle.start > interval.start) {
            result.push_back({interval.start, obstacle.start});
        }
        if (obstacle.end < interval.end) {
            result.push_back({obstacle.end, interval.end});
        }
    }
    std::sort(result.begin(), result.end(), [](const Interval& a, const Interval& b) { return a.start < b.start; });
    std::vector<Interval> merged;
    merged.reserve(result.size());
    for (const Interval& candidate : result) {
        if (candidate.end <= candidate.start) {
            continue;
        }
        if (merged.empty() || merged.back().end < candidate.start) {
            merged.push_back(candidate);
        } else {
            merged.back().end = std::max(merged.back().end, candidate.end);
        }
    }
    return merged;
}

std::vector<Interval> compute_free_intervals(const SDL_Rect& usable, const std::vector<SDL_Rect>& obstacles) {
    std::vector<Interval> free;
    if (usable.w <= 0) {
        return free;
    }
    free.push_back({usable.x, usable.x + usable.w});
    SDL_Rect usable_bounds = usable;
    for (const SDL_Rect& rect : obstacles) {
        SDL_Rect candidate = clamp_rect_to_view(rect);
        SDL_Rect clipped;
        if (SDL_IntersectRect(&usable_bounds, &candidate, &clipped) != SDL_TRUE) {
            continue;
        }
        if (!has_area(clipped)) {
            continue;
        }
        Interval occupied{clipped.x, clipped.x + clipped.w};
        free = subtract_interval(free, occupied);
    }
    if (free.empty()) {
        free.push_back({usable.x, usable.x + usable.w});
    }
    return free;
}

int clamp_dimension(int value, int limit) {
    if (limit <= 0) {
        return std::max(1, value);
    }
    return std::clamp(value, 1, limit);
}

int locate_position(int desired, int width, const std::vector<Interval>& intervals, const SDL_Rect& usable) {
    if (intervals.empty()) {
        int min_x = usable.x;
        int max_x = usable.x + std::max(0, usable.w - width);
        if (max_x < min_x) {
            max_x = min_x;
        }
        return std::clamp(desired, min_x, max_x);
    }

    for (const Interval& interval : intervals) {
        int available = interval.end - interval.start;
        if (available < width) {
            continue;
        }
        if (desired >= interval.end) {
            continue;
        }
        if (desired <= interval.start) {
            return interval.start;
        }
        if (desired + width <= interval.end) {
            return desired;
        }
        return interval.end - width;
    }

    for (auto it = intervals.rbegin(); it != intervals.rend(); ++it) {
        const Interval& interval = *it;
        int available = interval.end - interval.start;
        if (available < width) {
            continue;
        }
        return interval.end - width;
    }

    int min_x = usable.x;
    int max_x = usable.x + std::max(0, usable.w - width);
    if (max_x < min_x) {
        max_x = min_x;
    }
    return std::clamp(desired, min_x, max_x);
}

int resolve_panel_width(const FloatingPanelLayoutManager::PanelInfo& info, const SDL_Rect& usable) {
    if (!info.panel) {
        return 0;
    }
    SDL_Rect rect = info.panel->rect();
    int width = rect.w;
    if (width <= 0) {
        width = info.preferred_width;
    }
    if (width <= 0) {
        width = DockableCollapsible::kDefaultFloatingContentWidth;
    }
    width = clamp_dimension(width, usable.w);
    return width;
}

int resolve_panel_height(const FloatingPanelLayoutManager::PanelInfo& info, const SDL_Rect& usable) {
    if (!info.panel) {
        return 0;
    }
    SDL_Rect rect = info.panel->rect();
    int height = rect.h;
    if (height <= 0) {
        height = info.panel->height();
    }
    if (height <= 0) {
        height = info.preferred_height;
    }
    if (height <= 0) {
        height = 400;
    }
    height = clamp_dimension(height, usable.h);
    return height;
}

SDL_Rect sanitize_rect(const SDL_Rect& rect) {
    SDL_Rect result = rect;
    if (result.w < 0) {
        result.x += result.w;
        result.w = -result.w;
    }
    if (result.h < 0) {
        result.y += result.h;
        result.h = -result.h;
    }
    return result;
}

}

FloatingPanelLayoutManager& FloatingPanelLayoutManager::instance() {
    static FloatingPanelLayoutManager manager;
    return manager;
}

SDL_Rect FloatingPanelLayoutManager::computeUsableRect(const SDL_Rect& viewport,
                                                       const SDL_Rect& headerBounds,
                                                       const SDL_Rect& footerBounds,
                                                       const std::vector<SDL_Rect>& slidingContainers) {
    viewport_ = sanitize_rect(viewport);
    header_bounds_ = sanitize_rect(headerBounds);
    footer_bounds_ = sanitize_rect(footerBounds);
    sliding_rects_.clear();
    sliding_rects_.reserve(slidingContainers.size());
    for (const SDL_Rect& rect : slidingContainers) {
        SDL_Rect sanitized = sanitize_rect(rect);
        if (has_area(sanitized)) {
            sliding_rects_.push_back(sanitized);
        }
    }

    usable_rect_ = viewport_;
    if (usable_rect_.w <= 0 || usable_rect_.h <= 0) {
        usable_rect_ = SDL_Rect{0, 0, 0, 0};
        return usable_rect_;
    }

    int top = usable_rect_.y;
    int bottom = usable_rect_.y + usable_rect_.h;

    if (has_area(header_bounds_)) {
        int header_bottom = header_bounds_.y + header_bounds_.h;
        top = std::max(top, header_bottom + kHeaderToPanelPadding);
    }

    if (has_area(footer_bounds_)) {
        int footer_top = footer_bounds_.y;
        bottom = std::min(bottom, footer_top);
    }

    if (bottom < top) {
        bottom = top;
    }

    usable_rect_.y = top;
    usable_rect_.h = std::max(0, bottom - top);
    layoutTrackedPanels();
    return usable_rect_;
}

void FloatingPanelLayoutManager::layoutAll(const std::vector<PanelInfo>& panels) {
    struct ScopedLayoutFlag {
        FloatingPanelLayoutManager& manager;
        bool active = false;
        explicit ScopedLayoutFlag(FloatingPanelLayoutManager& m) : manager(m) {
            if (!manager.applying_layout_) {
                manager.applying_layout_ = true;
                active = true;
            }
        }
        ~ScopedLayoutFlag() {
            if (active) {
                manager.applying_layout_ = false;
            }
        }
    } guard(*this);

    if (usable_rect_.w <= 0 || usable_rect_.h <= 0) {
        return;
    }

    std::vector<const PanelInfo*> targets;
    targets.reserve(panels.size());
    for (const PanelInfo& info : panels) {
        if (!info.panel) {
            continue;
        }
        if (!info.force_layout && !info.panel->is_visible()) {
            continue;
        }
        if (user_placed_.count(info.panel) > 0) {
            continue;
        }
        targets.push_back(&info);
    }

    if (targets.empty()) {
        return;
    }

    std::vector<Interval> free_intervals = compute_free_intervals(usable_rect_, sliding_rects_);

    const int count = static_cast<int>(targets.size());
    std::vector<int> widths(count, 0);
    std::vector<int> heights(count, 0);
    for (int i = 0; i < count; ++i) {
        widths[i] = resolve_panel_width(*targets[i], usable_rect_);
        heights[i] = resolve_panel_height(*targets[i], usable_rect_);
    }

    int total_width = std::accumulate(widths.begin(), widths.end(), 0);
    if (count > 1) {
        total_width += kPanelGap * (count - 1);
    }

    int start_x = usable_rect_.x + (usable_rect_.w - total_width) / 2;
    int min_start = usable_rect_.x;
    int max_start = usable_rect_.x + std::max(0, usable_rect_.w - total_width);
    if (max_start < min_start) {
        max_start = min_start;
    }
    start_x = std::clamp(start_x, min_start, max_start);

    int current = start_x;
    for (int i = 0; i < count; ++i) {
        const PanelInfo& info = *targets[i];
        int width = widths[i];
        int height = heights[i];

        int x = locate_position(current, width, free_intervals, usable_rect_);
        int y = usable_rect_.y;
        if (count == 1) {
            y = usable_rect_.y + (usable_rect_.h - height) / 2;
        }
        int min_y = usable_rect_.y;
        int max_y = usable_rect_.y + std::max(0, usable_rect_.h - height);
        if (max_y < min_y) {
            max_y = min_y;
        }
        y = std::clamp(y, min_y, max_y);
        info.panel->set_position_from_layout_manager(x, y);

        current = x + width + kPanelGap;
    }
}

SDL_Point FloatingPanelLayoutManager::positionFor(const PanelInfo& panel, const SlidingParentInfo* parent) const {
    SDL_Point fallback{usable_rect_.x, usable_rect_.y};
    if (!panel.panel || usable_rect_.w <= 0 || usable_rect_.h <= 0) {
        return fallback;
    }

    std::vector<Interval> free_intervals = compute_free_intervals(usable_rect_, sliding_rects_);
    int width = resolve_panel_width(panel, usable_rect_);
    int height = resolve_panel_height(panel, usable_rect_);

    int desired_x = usable_rect_.x;
    if (parent) {
        if (parent->anchor_left) {
            desired_x = parent->bounds.x - parent->padding - width;
        } else {
            desired_x = parent->bounds.x + parent->bounds.w + parent->padding;
        }
    } else {
        desired_x = usable_rect_.x + usable_rect_.w / 2 - width / 2;
    }

    int x = locate_position(desired_x, width, free_intervals, usable_rect_);
    int y = usable_rect_.y;
    if (parent) {
        if (parent->align_top) {
            y = parent->bounds.y;
        } else {
            int parent_center = parent->bounds.y + parent->bounds.h / 2;
            y = parent_center - height / 2;
        }
    }

    int min_y = usable_rect_.y;
    int max_y = usable_rect_.y + std::max(0, usable_rect_.h - height);
    if (max_y < min_y) {
        max_y = min_y;
    }
    y = std::clamp(y, min_y, max_y);

    return SDL_Point{x, y};
}

void FloatingPanelLayoutManager::registerPanel(DockableCollapsible* panel) {
    if (!panel) {
        return;
    }
    if (isTracking(panel)) {
        return;
    }
    tracked_panels_.push_back(panel);
    layoutTrackedPanels();
}

void FloatingPanelLayoutManager::unregisterPanel(const DockableCollapsible* panel) {
    if (!panel) {
        return;
    }
    auto it = std::remove(tracked_panels_.begin(), tracked_panels_.end(), const_cast<DockableCollapsible*>(panel));
    if (it == tracked_panels_.end()) {
        return;
    }
    tracked_panels_.erase(it, tracked_panels_.end());
    user_placed_.erase(panel);
    layoutTrackedPanels();
}

void FloatingPanelLayoutManager::notifyPanelGeometryChanged(DockableCollapsible* panel) {
    if (!panel || !isTracking(panel) || applying_layout_) {
        return;
    }
    layoutTrackedPanels();
}

void FloatingPanelLayoutManager::notifyPanelContentChanged(DockableCollapsible* panel) {
    if (!panel || !isTracking(panel) || applying_layout_) {
        return;
    }
    layoutTrackedPanels();
}

void FloatingPanelLayoutManager::notifyPanelUserMoved(DockableCollapsible* panel) {
    if (!panel) {
        return;
    }
    user_placed_.insert(panel);
}

void FloatingPanelLayoutManager::layoutTrackedPanels() {
    if (applying_layout_ || tracked_panels_.empty()) {
        return;
    }

    std::vector<PanelInfo> panels;
    panels.reserve(tracked_panels_.size());
    for (DockableCollapsible* panel : tracked_panels_) {
        if (!panel) {
            continue;
        }
        if (!panel->is_visible() || !panel->is_floatable()) {
            continue;
        }
        if (user_placed_.count(panel) > 0) {
            continue;
        }
        PanelInfo info;
        info.panel = panel;
        panels.push_back(info);
    }

    if (panels.empty()) {
        return;
    }

    applying_layout_ = true;
    layoutAll(panels);
    applying_layout_ = false;
}

bool FloatingPanelLayoutManager::isTracking(const DockableCollapsible* panel) const {
    return std::find(tracked_panels_.begin(), tracked_panels_.end(), panel) != tracked_panels_.end();
}
