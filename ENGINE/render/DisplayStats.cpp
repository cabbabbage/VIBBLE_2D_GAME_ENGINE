#include "render/DisplayStats.hpp"

#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "render_pipeline/ScalingLogic.hpp"
#include "utils/input.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <vector>

namespace {

constexpr int kFontSize = 16;

std::vector<std::string> font_search_paths()
{
    std::vector<std::string> paths;

    if (const char* env_override = std::getenv("VIBBLE_STATS_FONT")) {
        if (env_override[0] != '\0') {
            paths.emplace_back(env_override);
        }
    }

#ifdef _WIN32
    paths.emplace_back("C:/Windows/Fonts/segoeui.ttf");
    paths.emplace_back("C:/Windows/Fonts/arial.ttf");
    paths.emplace_back("C:/Windows/Fonts/tahoma.ttf");
#elif defined(__APPLE__)
    paths.emplace_back("/System/Library/Fonts/SFNS.ttf");
    paths.emplace_back("/System/Library/Fonts/SFNSDisplay.ttf");
    paths.emplace_back("/System/Library/Fonts/Helvetica.ttc");
    paths.emplace_back("/System/Library/Fonts/Supplemental/Arial.ttf");
    paths.emplace_back("/System/Library/Fonts/Supplemental/Arial Unicode.ttf");
#else
    paths.emplace_back("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
    paths.emplace_back("/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed.ttf");
    paths.emplace_back("/usr/share/fonts/truetype/freefont/FreeSans.ttf");
    paths.emplace_back("/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf");
    paths.emplace_back("/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf");
#endif

    return paths;
}

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
    if (font_load_attempted_) {
        return;
    }
    font_load_attempted_ = true;
    for (const std::string& path : font_search_paths()) {
        if (path.empty()) {
            continue;
        }
        font_ = TTF_OpenFont(path.c_str(), kFontSize);
        if (font_) {
            font_load_attempted_ = false;
            font_warning_logged_ = false;
            break;
        }
    }

    if (!font_ && !font_warning_logged_) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[DisplayStats] Unable to load stats font. Set VIBBLE_STATS_FONT to a valid TTF file to restore text.");
        font_warning_logged_ = true;
    }
}

void DisplayStats::handle_input(const Input& input) {
    const bool ctrl_down = input.isScancodeDown(SDL_SCANCODE_LCTRL) || input.isScancodeDown(SDL_SCANCODE_RCTRL);
    if (ctrl_down && input.wasScancodePressed(SDL_SCANCODE_S)) {
        visible_ = !visible_;
        if (!visible_) {
            reset_frame_history();
        }
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

    const std::size_t average_base = tracked_names_.size();
    Row& avg_time_row = rows_[average_base];
    Row& avg_percent_row = rows_[average_base + 1];

    if (has_frame_history()) {
        const float avg_time = average_elapsed_ms();
        const float avg_percent = average_percent_diff();

        std::ostringstream avg_time_stream;
        avg_time_stream << "Average Frame: " << std::fixed << std::setprecision(2) << avg_time << " ms";
        avg_time_row.line = avg_time_stream.str();
        avg_time_row.found = true;

        std::ostringstream avg_percent_stream;
        avg_percent_stream << "Average Δ%: " << std::showpos << std::fixed << std::setprecision(2)
                           << avg_percent << "%";
        avg_percent_row.line = avg_percent_stream.str();
        avg_percent_row.found = true;
    } else {
        avg_time_row.line = "Average Frame: --";
        avg_time_row.found = false;
        avg_percent_row.line = "Average Δ%: --";
        avg_percent_row.found = false;
    }
}

void DisplayStats::record_frame_timing(float elapsed_ms, float target_ms, float early_ms, float late_ms)
{
    (void)early_ms;
    (void)late_ms;
    if (!visible_) {
        if (!frame_timing_samples_.empty()) {
            reset_frame_history();
        }
        return;
    }

    const Uint32 now_ms = SDL_GetTicks();
    const float delta_ms = elapsed_ms - target_ms;
    const float percent_diff = (target_ms > 0.0f) ? (delta_ms / target_ms) * 100.0f : 0.0f;

    FrameTimingSample sample{};
    sample.timestamp_ms = now_ms;
    sample.elapsed_ms = elapsed_ms;
    sample.target_ms = target_ms;
    sample.delta_ms = delta_ms;
    sample.percent_diff = percent_diff;

    append_frame_sample(sample);
}

void DisplayStats::render(SDL_Renderer* renderer) {
    if (!visible_) {
        return;
    }
    if (!renderer) {
        return;
    }
    ensure_font();

    const bool has_font = (font_ != nullptr);

    std::vector<SDL_Surface*> surfaces(rows_.size(), nullptr);
    std::vector<SDL_Texture*> textures(rows_.size(), nullptr);
    int text_max_width = 0;
    int text_total_height = 0;

    if (has_font) {
        for (std::size_t idx = 0; idx < rows_.size(); ++idx) {
            const std::string& text = rows_[idx].line;
            SDL_Surface* surf = text.empty() ? nullptr : TTF_RenderUTF8_Blended(font_, text.c_str(), text_color_);
            surfaces[idx] = surf;
            const int line_height = surf ? surf->h : kFontSize;
            text_total_height += line_height;
            if (idx + 1 < rows_.size()) {
                text_total_height += line_spacing_;
            }
            if (surf) {
                text_max_width = std::max(text_max_width, surf->w);
            }
        }
    }

    const int content_width = std::max(kChartWidth, text_max_width);
    const int chart_width = content_width;
    const int chart_height = kChartHeight;
    const int text_block_height = has_font ? (kChartTextSpacing + text_total_height) : 0;
    const int total_height = padding_ * 2 + chart_height + text_block_height;

    SDL_Rect background{
        margin_,
        margin_,
        content_width + padding_ * 2,
        total_height
    };

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, bg_color_.r, bg_color_.g, bg_color_.b, bg_color_.a);
    SDL_RenderFillRect(renderer, &background);
    SDL_SetRenderDrawColor(renderer, border_color_.r, border_color_.g, border_color_.b, border_color_.a);
    SDL_RenderDrawRect(renderer, &background);

    SDL_Rect chart_rect{
        margin_ + padding_,
        margin_ + padding_,
        chart_width,
        chart_height
    };

    SDL_SetRenderDrawColor(renderer, chart_bg_color_.r, chart_bg_color_.g, chart_bg_color_.b, chart_bg_color_.a);
    SDL_RenderFillRect(renderer, &chart_rect);

    const int target_y = chart_rect.y + chart_rect.h / 2;
    SDL_SetRenderDrawColor(renderer, chart_target_line_color_.r, chart_target_line_color_.g,
                           chart_target_line_color_.b, chart_target_line_color_.a);
    SDL_RenderDrawLine(renderer, chart_rect.x, target_y, chart_rect.x + chart_rect.w - 1, target_y);

    if (frame_timing_samples_.size() >= 2) {
        const Uint32 newest_time = frame_timing_samples_.back().timestamp_ms;
        const float history_span = static_cast<float>(kHistoryDurationMs);
        int prev_x = 0;
        int prev_y = 0;
        bool have_prev = false;

        SDL_SetRenderDrawColor(renderer, chart_line_color_.r, chart_line_color_.g, chart_line_color_.b, chart_line_color_.a);

        for (const FrameTimingSample& sample : frame_timing_samples_) {
            const Uint32 age_ticks = newest_time - sample.timestamp_ms;
            const float age_ms = static_cast<float>(age_ticks);
            const float normalized_time = std::clamp(1.0f - (age_ms / history_span), 0.0f, 1.0f);
            const int x = chart_rect.x + static_cast<int>(normalized_time * static_cast<float>(chart_rect.w - 1));

            const float clamped_delta = std::clamp(sample.delta_ms, -kChartMaxDeltaMs, kChartMaxDeltaMs);
            const float normalized_delta = clamped_delta / kChartMaxDeltaMs;
            const float y_offset = normalized_delta * (chart_rect.h / 2.0f);
            const float y_float = static_cast<float>(target_y) - y_offset;
            const int y = static_cast<int>(std::round(y_float));

            if (have_prev) {
                SDL_RenderDrawLine(renderer, prev_x, prev_y, x, y);
            }
            prev_x = x;
            prev_y = y;
            have_prev = true;
        }
    }

    if (has_font) {
        int pen_y = chart_rect.y + chart_rect.h + kChartTextSpacing;
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
}

void DisplayStats::append_frame_sample(const FrameTimingSample& sample) {
    frame_timing_samples_.push_back(sample);
    sum_elapsed_ms_ += sample.elapsed_ms;
    sum_percent_diff_ += sample.percent_diff;
    prune_old_samples(sample.timestamp_ms);
}

void DisplayStats::prune_old_samples(Uint32 now_ms) {
    while (!frame_timing_samples_.empty()) {
        const FrameTimingSample& front = frame_timing_samples_.front();
        const Uint32 expiry = front.timestamp_ms + kHistoryDurationMs;
        if (!SDL_TICKS_PASSED(now_ms, expiry)) {
            break;
        }
        sum_elapsed_ms_ -= front.elapsed_ms;
        sum_percent_diff_ -= front.percent_diff;
        frame_timing_samples_.pop_front();
    }

    if (frame_timing_samples_.empty()) {
        sum_elapsed_ms_ = 0.0;
        sum_percent_diff_ = 0.0;
    }
}

void DisplayStats::reset_frame_history() {
    frame_timing_samples_.clear();
    sum_elapsed_ms_ = 0.0;
    sum_percent_diff_ = 0.0;
}

float DisplayStats::average_elapsed_ms() const {
    if (frame_timing_samples_.empty()) {
        return 0.0f;
    }
    return static_cast<float>(sum_elapsed_ms_ / static_cast<double>(frame_timing_samples_.size()));
}

float DisplayStats::average_percent_diff() const {
    if (frame_timing_samples_.empty()) {
        return 0.0f;
    }
    return static_cast<float>(sum_percent_diff_ / static_cast<double>(frame_timing_samples_.size()));
}
