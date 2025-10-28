#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "render_pipeline/ScalingLogic.hpp"

#include <SDL.h>

#include <filesystem>
#include <stdexcept>

namespace {
class SDLTimerGuard {
public:
    SDLTimerGuard() {
        SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
        if (SDL_Init(SDL_INIT_TIMER) != 0) {
            throw std::runtime_error(SDL_GetError());
        }
    }

    ~SDLTimerGuard() {
        SDL_Quit();
    }
};

SDLTimerGuard& ensure_sdl_timer() {
    static SDLTimerGuard guard;
    return guard;
}
}

TEST_CASE("Usage sampling pending flag toggles during record and flush") {
    ensure_sdl_timer();

    using render_pipeline::ScalingLogic;

    const std::filesystem::path temp_dir = std::filesystem::path("TEMP") / "tests";
    std::filesystem::create_directories(temp_dir);
    const std::filesystem::path storage_path = temp_dir / "scaling_logic_usage_sampling.json";
    if (std::filesystem::exists(storage_path)) {
        std::filesystem::remove(storage_path);
    }

    ScalingLogic::ShutdownUsageSampling();
    ScalingLogic::ConfigureUsageStorage(storage_path);
    ScalingLogic::SetUsageTrackingEnabled(true);

    CHECK_FALSE(ScalingLogic::UsageSamplingPending());

    ScalingLogic::RecordUsage("test_asset", 1.0f, 0.5f);
    CHECK(ScalingLogic::UsageSamplingPending());

    ScalingLogic::FlushUsageData();
    CHECK_FALSE(ScalingLogic::UsageSamplingPending());

    ScalingLogic::RecordUsage("test_asset", 0.8f, 0.75f);
    CHECK(ScalingLogic::UsageSamplingPending());

    const bool enabled_after_toggle = ScalingLogic::ToggleUsageTracking();
    CHECK_FALSE(enabled_after_toggle);

    ScalingLogic::FlushUsageData();
    CHECK_FALSE(ScalingLogic::UsageSamplingPending());

    ScalingLogic::SetUsageTrackingEnabled(true);
    ScalingLogic::ResetAssetUsage("test_asset");
    CHECK_FALSE(ScalingLogic::UsageSamplingPending());

    ScalingLogic::ShutdownUsageSampling();

    if (std::filesystem::exists(storage_path)) {
        std::filesystem::remove(storage_path);
    }
}
