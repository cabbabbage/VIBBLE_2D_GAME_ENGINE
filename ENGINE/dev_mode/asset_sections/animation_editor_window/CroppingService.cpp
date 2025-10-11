#include "CroppingService.hpp"

#include <SDL.h>
#include <SDL_image.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <string>

namespace animation_editor {

namespace {

using SurfacePtr = std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)>;

SurfacePtr make_surface_ptr(SDL_Surface* surface) {
    return SurfacePtr(surface, SDL_FreeSurface);
}

}

CroppingService::CroppingService() = default;

bool CroppingService::is_numbered_png(const std::filesystem::path& path) const {
    const std::string extension = path.extension().string();
    if (extension.empty()) {
        return false;
    }

    std::string lower_ext;
    lower_ext.reserve(extension.size());
    for (char ch : extension) {
        lower_ext.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }

    if (lower_ext != ".png") {
        return false;
    }

    const std::string stem = path.stem().string();
    if (stem.empty()) {
        return false;
    }

    return std::all_of(stem.begin(), stem.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; });
}

SDL_Surface* CroppingService::load_surface_rgba(const std::filesystem::path& path) {
    SurfacePtr loaded = make_surface_ptr(IMG_Load(path.string().c_str()));
    if (!loaded) {
        return nullptr;
    }

    SurfacePtr converted = make_surface_ptr(SDL_ConvertSurfaceFormat(loaded.get(), SDL_PIXELFORMAT_RGBA8888, 0));
    if (!converted) {
        return nullptr;
    }

    loaded.reset();
    return converted.release();
}

bool CroppingService::save_surface_as_png(SDL_Surface* surface, const std::filesystem::path& path) {
    if (!surface) {
        return false;
    }

#if SDL_IMAGE_VERSION_ATLEAST(2, 0, 0)
    if (IMG_SavePNG(surface, path.string().c_str()) == 0) {
        return true;
    }
#endif

    std::filesystem::path bmp_path = path;
    bmp_path.replace_extension(".bmp");
    return SDL_SaveBMP(surface, bmp_path.string().c_str()) == 0;
}

void CroppingService::compute_union_bounds(const std::vector<std::filesystem::path>& frames) {
    bounds_ = {};

    if (frames.empty()) {
        return;
    }

    const Uint8 alpha_threshold = 0;

    int union_left = 0;
    int union_top = 0;
    int union_right = 0;
    int union_bottom = 0;
    bool union_initialised = false;

    for (const auto& frame : frames) {
        SurfacePtr surface = make_surface_ptr(load_surface_rgba(frame));
        if (!surface) {
            continue;
        }

        if (bounds_.base_width == 0 || bounds_.base_height == 0) {
            bounds_.base_width = surface->w;
            bounds_.base_height = surface->h;
        }

        if (surface->w <= 0 || surface->h <= 0) {
            continue;
        }

        if (SDL_MUSTLOCK(surface.get()) && SDL_LockSurface(surface.get()) != 0) {
            continue;
        }

        const int width = surface->w;
        const int height = surface->h;
        const int pitch = surface->pitch;
        const Uint8* pixels = static_cast<const Uint8*>(surface->pixels);

        int local_left = width;
        int local_top = height;
        int local_right = 0;
        int local_bottom = 0;
        bool has_visible_pixel = false;

        for (int y = 0; y < height; ++y) {
            const Uint8* row = pixels + y * pitch;
            for (int x = 0; x < width; ++x) {
                const Uint8 alpha = row[x * 4 + 3];
                if (alpha > alpha_threshold) {
                    has_visible_pixel = true;
                    if (x < local_left) local_left = x;
                    if (y < local_top) local_top = y;
                    if (x + 1 > local_right) local_right = x + 1;
                    if (y + 1 > local_bottom) local_bottom = y + 1;
                }
            }
        }

        if (SDL_MUSTLOCK(surface.get())) {
            SDL_UnlockSurface(surface.get());
        }

        if (!has_visible_pixel) {
            continue;
        }

        if (!union_initialised) {
            union_left = local_left;
            union_top = local_top;
            union_right = local_right;
            union_bottom = local_bottom;
            union_initialised = true;
        } else {
            union_left = std::min(union_left, local_left);
            union_top = std::min(union_top, local_top);
            union_right = std::max(union_right, local_right);
            union_bottom = std::max(union_bottom, local_bottom);
        }
    }

    if (!union_initialised || bounds_.base_width <= 0 || bounds_.base_height <= 0) {
        bounds_ = {};
        return;
    }

    bounds_.left = union_left;
    bounds_.top = union_top;
    bounds_.right = std::max(0, bounds_.base_width - union_right);
    bounds_.bottom = std::max(0, bounds_.base_height - union_bottom);
    bounds_.valid = bounds_.cropped_width() > 0 && bounds_.cropped_height() > 0;
}

void CroppingService::crop_images_with_bounds(const std::vector<std::filesystem::path>& frames) {
    if (!bounds_.valid) {
        return;
    }

    const int target_width = bounds_.cropped_width();
    const int target_height = bounds_.cropped_height();
    if (target_width <= 0 || target_height <= 0) {
        return;
    }

    for (const auto& frame : frames) {
        SurfacePtr surface = make_surface_ptr(load_surface_rgba(frame));
        if (!surface) {
            continue;
        }

        const int src_width = surface->w;
        const int src_height = surface->h;

        if (src_width <= 0 || src_height <= 0) {
            continue;
        }

        const int left = std::min(bounds_.left, std::max(0, src_width - 1));
        const int top = std::min(bounds_.top, std::max(0, src_height - 1));
        const int right_margin = std::min(bounds_.right, std::max(0, src_width - left));
        const int bottom_margin = std::min(bounds_.bottom, std::max(0, src_height - top));

        const int crop_width = std::max(0, src_width - left - right_margin);
        const int crop_height = std::max(0, src_height - top - bottom_margin);

        if (crop_width <= 0 || crop_height <= 0) {
            continue;
        }

        SurfacePtr cropped = make_surface_ptr(SDL_CreateRGBSurfaceWithFormat(0, crop_width, crop_height, 32, SDL_PIXELFORMAT_RGBA8888));
        if (!cropped) {
            continue;
        }

        SDL_Rect src_rect{left, top, crop_width, crop_height};
        SDL_Rect dst_rect{0, 0, crop_width, crop_height};

        if (SDL_BlitSurface(surface.get(), &src_rect, cropped.get(), &dst_rect) != 0) {
            continue;
        }

        save_surface_as_png(cropped.get(), frame);
    }
}

}

