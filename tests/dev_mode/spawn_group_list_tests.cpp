#define DOCTEST_CONFIG_IMPLEMENT_WITHOUT_MAIN
#include "doctest/doctest.h"

#include "dev_mode/spawn_group_config/SpawnGroupConfig.hpp"

#include <SDL.h>
#include <SDL_ttf.h>

#include <nlohmann/json.hpp>

#include <stdexcept>
#include <string>

namespace {
class SDLSubsystemGuard {
public:
    SDLSubsystemGuard() {
        SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            throw std::runtime_error(SDL_GetError());
        }
        if (TTF_Init() != 0) {
            std::string err = TTF_GetError();
            SDL_Quit();
            throw std::runtime_error(err);
        }
    }

    ~SDLSubsystemGuard() {
        TTF_Quit();
        SDL_Quit();
    }
};

SDLSubsystemGuard& ensure_sdl() {
    static SDLSubsystemGuard guard;
    return guard;
}
}  // namespace

class SpawnGroupConfigTestAccessor {
public:
    static void TriggerCallbacks(SpawnGroupConfig& config,
                                 const nlohmann::json& entry,
                                 const SpawnGroupConfig::ChangeSummary& summary) {
        config.fire_entry_callbacks(entry, summary);
    }
};

TEST_CASE("SpawnGroupConfig entry callbacks react to change summary flags") {
    ensure_sdl();

    using nlohmann::json;

    json entry = json{{"spawn_id", "alpha"},
                      {"display_name", "Alpha"},
                      {"position", "Random"},
                      {"min_number", 1},
                      {"max_number", 2},
                      {"candidates", json::array({json{{"name", "null"}, {"chance", 0}}})}};

    SpawnGroupConfig config;

    bool method_called = false;
    bool quantity_called = false;
    bool candidates_called = false;
    int quantity_min = 0;
    int quantity_max = 0;
    std::string method_name;

    SpawnGroupConfig::EntryCallbacks callbacks;
    callbacks.on_method_changed = [&](const std::string& method) {
        method_called = true;
        method_name = method;
    };
    callbacks.on_quantity_changed = [&](int min_value, int max_value) {
        quantity_called = true;
        quantity_min = min_value;
        quantity_max = max_value;
    };
    callbacks.on_candidates_changed = [&](const json& updated) {
        candidates_called = true;
        REQUIRE(updated.contains("candidates"));
        CHECK(updated["candidates"].is_array());
    };

    config.bind_entry(entry, callbacks);

    SpawnGroupConfig::ChangeSummary summary;
    summary.method_changed = true;
    summary.quantity_changed = true;
    summary.candidates_changed = true;
    summary.method = "Perimeter";

    SpawnGroupConfigTestAccessor::TriggerCallbacks(config, entry, summary);

    CHECK(method_called);
    CHECK_EQ(method_name, std::string("Perimeter"));
    CHECK(quantity_called);
    CHECK_EQ(quantity_min, 1);
    CHECK_EQ(quantity_max, 2);
    CHECK(candidates_called);
}

