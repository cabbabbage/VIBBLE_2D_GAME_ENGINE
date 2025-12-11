#pragma once

#include <SDL.h>

#include <unordered_set>
#include <vector>

class DockableCollapsible;

class FloatingPanelLayoutManager {
public:
    struct PanelInfo {
        DockableCollapsible* panel = nullptr;
        int preferred_width = 0;
        int preferred_height = 0;
        bool force_layout = false;
};

    struct SlidingParentInfo {
        SDL_Rect bounds{0, 0, 0, 0};
        int padding = 16;
        bool anchor_left = true;
        bool align_top = true;
};

    static FloatingPanelLayoutManager& instance();

    SDL_Rect computeUsableRect(const SDL_Rect& viewport, const SDL_Rect& headerBounds, const SDL_Rect& footerBounds, const std::vector<SDL_Rect>& slidingContainers);

    SDL_Rect usableRect() const { return usable_rect_; }

    void layoutAll(const std::vector<PanelInfo>& panels);

    SDL_Point positionFor(const PanelInfo& panel, const SlidingParentInfo* parent) const;

    void registerPanel(DockableCollapsible* panel);
    void unregisterPanel(const DockableCollapsible* panel);
    void notifyPanelGeometryChanged(DockableCollapsible* panel);
    void notifyPanelContentChanged(DockableCollapsible* panel);
    void notifyPanelUserMoved(DockableCollapsible* panel);

private:
    FloatingPanelLayoutManager() = default;

    void layoutTrackedPanels();
    bool isTracking(const DockableCollapsible* panel) const;

    SDL_Rect viewport_{0, 0, 0, 0};
    SDL_Rect header_bounds_{0, 0, 0, 0};
    SDL_Rect footer_bounds_{0, 0, 0, 0};
    SDL_Rect usable_rect_{0, 0, 0, 0};
    std::vector<SDL_Rect> sliding_rects_{};
    std::vector<DockableCollapsible*> tracked_panels_{};
    bool applying_layout_ = false;
    std::unordered_set<const DockableCollapsible*> user_placed_{};
};

