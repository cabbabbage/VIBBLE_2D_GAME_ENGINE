#define DOCTEST_CONFIG_IMPLEMENT_WITHOUT_MAIN
#include "doctest/doctest.h"

#include "dev_mode/spawn_group_config/SpawnGroupConfig.hpp"

#include <SDL.h>
#include <SDL_ttf.h>

#include <nlohmann/json.hpp>

#include <stdexcept>
#include <string>
#include <vector>

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
} // namespace

TEST_CASE("SpawnGroupConfig entries survive JSON array replacement") {
    ensure_sdl();

    using nlohmann::json;

    json groups = json::array({
        json{{"spawn_id", "alpha"}, {"display_name", "Alpha"}, {"min_number", 1}, {"max_number", 1}}
    });

    SpawnGroupConfig list;
    std::vector<std::string> labels;
    auto on_change = []() {};
    auto on_entry_change = [](const json&, const SpawnGroupConfig::ChangeSummary&) {};
    auto configure = [&labels](SpawnGroupConfig::EntryController&, const json& entry) {
        labels.push_back(entry.value("display_name", std::string{}));
    };

    list.load(groups, on_change, on_entry_change, configure);

    SpawnGroupConfig::Rows rows;
    list.append_rows(rows);
    REQUIRE_FALSE(rows.empty());
    REQUIRE_EQ(labels.size(), 1);
    CHECK_EQ(labels[0], std::string("Alpha"));

    labels.clear();

    json replacement = json::array({
        json{{"spawn_id", "alpha"}, {"display_name", "Gamma"}, {"min_number", 2}, {"max_number", 3}}
    });
    groups = replacement;

    rows.clear();
    list.append_rows(rows);
    REQUIRE_FALSE(rows.empty());

    list.refresh_row_configuration();

    REQUIRE_EQ(labels.size(), 1);
    CHECK_EQ(labels[0], std::string("Gamma"));
}

