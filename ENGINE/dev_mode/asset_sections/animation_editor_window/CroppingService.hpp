#pragma once

#include <algorithm>
#include <filesystem>
#include <vector>

struct SDL_Surface;

namespace animation_editor {

class CroppingService {
  public:
    CroppingService();

    bool is_numbered_png(const std::filesystem::path& path) const;
    void compute_union_bounds(const std::vector<std::filesystem::path>& frames);
    void crop_images_with_bounds(const std::vector<std::filesystem::path>& frames);

  private:
    struct CropBounds {
        int top = 0;
        int bottom = 0;
        int left = 0;
        int right = 0;
        int base_width = 0;
        int base_height = 0;
        bool valid = false;

        [[nodiscard]] int cropped_width() const {
            return std::max(0, base_width - left - right);
        }

        [[nodiscard]] int cropped_height() const {
            return std::max(0, base_height - top - bottom);
        }
    } bounds_;

    static bool save_surface_as_png(SDL_Surface* surface, const std::filesystem::path& path);
    static SDL_Surface* load_surface_rgba(const std::filesystem::path& path);
};

}

