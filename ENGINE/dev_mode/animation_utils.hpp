#pragma once

#include <SDL.h>
#include <SDL_image.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace animation {

// Helper to check if file name is a numbered PNG (e.g., "0001.png").
bool is_numbered_png(const std::string& filename);

// Return sorted list of numbered PNG files inside a folder.
std::vector<std::filesystem::path> get_image_paths(const std::filesystem::path& folder);

struct Bounds {
    int top{0}, bottom{0}, left{0}, right{0};
    int base_w{0}, base_h{0};
};

// Compute union bounds across all images. If no opaque pixels are found, all
// margins are zero. The alpha threshold treats alpha values <= threshold as
// transparent.
Bounds compute_union_bounds(const std::vector<std::filesystem::path>& image_paths,
                           int alpha_threshold = 0);

// Crop each image in-place using the provided margins. Returns count of images
// successfully cropped.
int crop_images_with_bounds(const std::vector<std::filesystem::path>& image_paths,
                            int crop_top,
                            int crop_bottom,
                            int crop_left,
                            int crop_right);

// --------------------------------------------------------------
// Undo history manager (deep snapshots using nlohmann::json)
// --------------------------------------------------------------
class HistoryManager {
public:
    explicit HistoryManager(size_t limit = 200);
    void snapshot(const nlohmann::json& data);
    bool can_undo() const;
    std::optional<nlohmann::json> undo();

private:
    std::vector<nlohmann::json> stack_;
    size_t limit_;
};

// --------------------------------------------------------------
// View state capture/restore interfaces
// --------------------------------------------------------------
struct IViewWindow {
    virtual ~IViewWindow() = default;
    virtual std::string geometry() const = 0;
    virtual void set_geometry(const std::string& g) = 0;
};

struct IViewCanvas {
    virtual ~IViewCanvas() = default;
    virtual float zoom() const = 0;
    virtual void set_zoom(float z) = 0;
    virtual float xview() const = 0;
    virtual float yview() const = 0;
    virtual void set_xview(float v) = 0;
    virtual void set_yview(float v) = 0;
};

struct ViewState {
    std::string geometry;
    float zoom{1.0f};
    float xview{0.0f};
    float yview{0.0f};
};

class ViewStateManager {
public:
    ViewState capture(const IViewWindow& win, const IViewCanvas& canvas) const;
    void apply(IViewWindow& win, IViewCanvas& canvas, const ViewState& state) const;
};

// --------------------------------------------------------------
// Movement modal (simplified placeholder)
// --------------------------------------------------------------
class MovementModal {
public:
    using Position = std::pair<int, int>;

    MovementModal();
    void open(const std::vector<Position>& positions);
    bool is_open() const;

    // Event/render stubs
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r);

    const std::vector<Position>& positions() const { return positions_; }

private:
    bool open_{false};
    std::vector<Position> positions_;
    HistoryManager history_;
    int current_frame_{0};
};

} // namespace animation

