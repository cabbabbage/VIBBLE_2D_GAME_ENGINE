#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include <array>
#include <deque>
#include <string>

class Input;
class Assets;

class DisplayStats {
public:
    explicit DisplayStats(SDL_Renderer* renderer);
    ~DisplayStats();

    DisplayStats(const DisplayStats&) = delete;
    DisplayStats& operator=(const DisplayStats&) = delete;

    void handle_input(const Input& input);
    void update(const Assets& assets);
    void render(SDL_Renderer* renderer);
    void record_frame_timing(float elapsed_ms, float target_ms, float early_ms, float late_ms);

private:
    struct Row {
        std::string line;
        bool found = false;
    };

    struct FrameTimingSample {
        Uint32 timestamp_ms = 0;
        float elapsed_ms = 0.0f;
        float target_ms = 0.0f;
        float delta_ms = 0.0f;
        float percent_diff = 0.0f;
    };

    void append_frame_sample(const FrameTimingSample& sample);
    void prune_old_samples(Uint32 now_ms);
    void reset_frame_history();
    [[nodiscard]] float average_elapsed_ms() const;
    [[nodiscard]] float average_percent_diff() const;
    [[nodiscard]] bool has_frame_history() const { return !frame_timing_samples_.empty(); }

    void ensure_font();

    TTF_Font* font_ = nullptr;
    bool visible_ = false;
    static constexpr Uint32 kHistoryDurationMs = 20'000;  // 20 seconds
    static constexpr int kChartWidth = 320;
    static constexpr int kChartHeight = 120;
    static constexpr float kChartMaxDeltaMs = 25.0f;
    static constexpr int kChartTextSpacing = 10;

    std::array<Row, 4> rows_{};
    const std::array<std::string, 2> tracked_names_{"Vibble", "Tree"};
    SDL_Color text_color_{255, 255, 255, 255};
    SDL_Color bg_color_{0, 0, 0, 180};
    SDL_Color border_color_{255, 255, 255, 200};
    SDL_Color chart_bg_color_{20, 20, 20, 200};
    SDL_Color chart_line_color_{80, 220, 255, 255};
    SDL_Color chart_target_line_color_{255, 255, 255, 180};
    int padding_ = 8;
    int line_spacing_ = 4;
    int margin_ = 10;
    std::deque<FrameTimingSample> frame_timing_samples_;
    double sum_elapsed_ms_ = 0.0;
    double sum_percent_diff_ = 0.0;
};

