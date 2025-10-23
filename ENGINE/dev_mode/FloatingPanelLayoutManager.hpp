#pragma once

#include <SDL.h>

#include <vector>

class DockableCollapsible;

// FloatingPanelLayoutManager centralizes the layout rules for floatable DockableCollapsible
// panels that appear in developer tooling. Callers first describe the usable workspace via
// computeUsableRect, then provide the panels that should be positioned via layoutAll or
// query a single recommended location through positionFor.
class FloatingPanelLayoutManager {
public:
    struct PanelInfo {
        DockableCollapsible* panel = nullptr;  // Panel that will be repositioned.
        int preferred_width = 0;               // Fallback width (in pixels) if the panel reports none.
        int preferred_height = 0;              // Fallback height (in pixels) if the panel reports none.
        bool force_layout = false;             // Layout even if the panel is currently hidden.
    };

    struct SlidingParentInfo {
        SDL_Rect bounds{0, 0, 0, 0};  // Bounding box of the sliding container to align against.
        int padding = 16;             // Horizontal gap that should separate the panel from the container.
        bool anchor_left = true;      // When true place the panel to the left of the container, otherwise to the right.
        bool align_top = true;        // Align panel tops when true, otherwise vertically center relative to the bounds.
    };

    static FloatingPanelLayoutManager& instance();

    SDL_Rect computeUsableRect(const SDL_Rect& viewport,
                               const SDL_Rect& headerBounds,
                               const SDL_Rect& footerBounds,
                               const std::vector<SDL_Rect>& slidingContainers);

    void layoutAll(const std::vector<PanelInfo>& panels);

    SDL_Point positionFor(const PanelInfo& panel, const SlidingParentInfo* parent) const;

private:
    FloatingPanelLayoutManager() = default;

    SDL_Rect viewport_{0, 0, 0, 0};
    SDL_Rect header_bounds_{0, 0, 0, 0};
    SDL_Rect footer_bounds_{0, 0, 0, 0};
    SDL_Rect usable_rect_{0, 0, 0, 0};
    std::vector<SDL_Rect> sliding_rects_{};
};

