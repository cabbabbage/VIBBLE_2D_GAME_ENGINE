#include "CroppingService.hpp"

namespace animation_editor {

CroppingService::CroppingService() = default;

bool CroppingService::is_numbered_png(const std::filesystem::path& path) const {
    (void)path;
    // TODO: Match numbered filename pattern used by animation importer.
    return false;
}

void CroppingService::compute_union_bounds(const std::vector<std::filesystem::path>& frames) {
    (void)frames;
    // TODO: Inspect images to calculate shared cropping rectangle.
}

void CroppingService::crop_images_with_bounds(const std::vector<std::filesystem::path>& frames) {
    (void)frames;
    // TODO: Apply previously computed bounds to crop each image.
}

}  // namespace animation_editor

