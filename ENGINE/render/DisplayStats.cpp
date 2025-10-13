#include "render/DisplayStats.hpp"

#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "render_pipeline/ScalingLogic.hpp"
#include "utils/input.hpp"

#include <algorithm>
#include <cstddef>
#include <iomanip>
#include <sstream>

namespace {

#ifdef _WIN32
constexpr const char* kDefaultFontPath = "C:/Windows/Fonts/segoeui.ttf";
#else
constexpr const char* kDefaultFontPath = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
#endif
constexpr int kFontSize = 16;

}  // namespace

DisplayStats::DisplayStats(SDL_Renderer* /*renderer*/) {
    ensure_font();
}

DisplayStats::~DisplayStats() {
    if (font_) {
        TTF_CloseFont(font_);
        font_ = nullptr;
    }
}

void DisplayStats::ensure_font() {
    if (font_) {
        return;
    }
    font_ = TTF_OpenFont(kDefaultFontPath, kFontSize);
}

void DisplayStats::handle_input(const Input& input) {
    const bool ctrl_down = input.isScancodeDown(SDL_SCANCODE_LCTRL) || input.isScancodeDown(SDL_SCANCODE_RCTRL);
    if (ctrl_down && input.wasScancodePressed(SDL_SCANCODE_S)) {
        visible_ = !visible_;
    }
}

void DisplayStats::update(const Assets& assets) {
    if (!visible_) {
        return;
    }

    for (std::size_t idx = 0; idx < tracked_names_.size(); ++idx) {
        Row& row = rows_[idx];
        const std::string& target = tracked_names_[idx];
        Asset* asset = assets.find_asset_by_name(target);
        if (!asset) {
            row.found = false;
            row.line = target + " | not found";
            continue;
        }

        const Asset::ScaleUsageStats& stats = asset->last_scale_usage();
        const float requested_percent = stats.requested_percent();
        const int texture_percent = render_pipeline::ScalingLogic::ScalePercent(stats.variant_index);
        const float applied_percent = stats.remainder_percent();

        std::ostringstream oss;
        oss << target
            << " | Total: " << std::fixed << std::setprecision(1) << requested_percent << "%"
            << " | Texture: " << texture_percent << "%"
            << " | Applied: " << std::fixed << std::setprecision(1) << applied_percent << "%";
        row.line = oss.str();
        row.found = true;
    }
}

void DisplayStats::record_frame_timing(float elapsed_ms, float target_ms, float early_ms, float late_ms)
{
    constexpr std::size_t kMaxSamples = 256;
    if (frame_timing_samples_.size() >= kMaxSamples) {
        frame_timing_samples_.erase(frame_timing_samples_.begin());
    }
    frame_timing_samples_.push_back(FrameTimingSample{elapsed_ms, target_ms, early_ms, late_ms});
}

void DisplayStats::render(SDL_Renderer* renderer) {
    if (!visible_) {
        return;
    }
    if (!renderer) {
        return;
    }
    ensure_font();
    if (!font_) {
        return;
    }

    std::array<SDL_Surface*, 2> surfaces{};
    std::array<SDL_Texture*, 2> textures{};
    int max_width = 0;
    int total_height = padding_ * 2;

    for (std::size_t idx = 0; idx < rows_.size(); ++idx) {
        const std::string& text = rows_[idx].line;
        SDL_Surface* surf = text.empty() ? nullptr : TTF_RenderUTF8_Blended(font_, text.c_str(), text_color_);
        surfaces[idx] = surf;
        if (surf) {
            max_width = std::max(max_width, surf->w);
            total_height += surf->h;
        } else {
            total_height += kFontSize;
        }
        if (idx + 1 < rows_.size()) {
            total_height += line_spacing_;
        }
    }

    SDL_Rect background{
        margin_,
        margin_,
        max_width + padding_ * 2,
        total_height
    };

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, bg_color_.r, bg_color_.g, bg_color_.b, bg_color_.a);
    SDL_RenderFillRect(renderer, &background);
    SDL_SetRenderDrawColor(renderer, border_color_.r, border_color_.g, border_color_.b, border_color_.a);
    SDL_RenderDrawRect(renderer, &background);

    int pen_y = margin_ + padding_;
    for (std::size_t idx = 0; idx < rows_.size(); ++idx) {
        SDL_Surface* surf = surfaces[idx];
        int line_height = kFontSize;
        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
            textures[idx] = tex;
            SDL_Rect dst{
                margin_ + padding_,
                pen_y,
                surf->w,
                surf->h
            };
            if (tex) {
                SDL_RenderCopy(renderer, tex, nullptr, &dst);
            }
            line_height = surf->h;
        }
        pen_y += line_height + line_spacing_;
    }

    for (SDL_Texture* tex : textures) {
        if (tex) {
            SDL_DestroyTexture(tex);
        }
    }
    for (SDL_Surface* surf : surfaces) {
        if (surf) {
            SDL_FreeSurface(surf);
        }
    }
}
