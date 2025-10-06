#pragma once

#include <filesystem>
#include <vector>

namespace animation_editor {

class CroppingService {
  public:
    CroppingService();

    bool is_numbered_png(const std::filesystem::path& path) const;
    void compute_union_bounds(const std::vector<std::filesystem::path>& frames);
    void crop_images_with_bounds(const std::vector<std::filesystem::path>& frames);

  private:
    // TODO: Store calculated cropping bounds for reuse across operations.
};

}  // namespace animation_editor

