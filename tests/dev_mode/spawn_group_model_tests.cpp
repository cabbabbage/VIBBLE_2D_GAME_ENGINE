#define DOCTEST_CONFIG_IMPLEMENT_WITHOUT_MAIN
#include "doctest/doctest.h"

#include <nlohmann/json.hpp>

#include "dev_mode/room_config/spawn_group_model.hpp"

namespace model = vibble::dev_mode::room_config::model;

TEST_CASE("spawn_group_from_json reads area name and method config") {
    nlohmann::json entry = {
        {"spawn_id", "sg-edge"},
        {"display_name", "Edge Spawn"},
        {"area", "center"},
        {"position", "Edge"},
        {"min_number", 2},
        {"max_number", 4},
        {"edge_inset_percent", 150},
        {"candidates", nlohmann::json::array({
            nlohmann::json{{"name", "alpha"}, {"chance", 5}},
            nlohmann::json{{"name", "beta"}, {"weight", "7.5"}}
        })}
    };

    const model::SpawnGroup group = model::spawn_group_from_json(entry);

    CHECK(group.id == "sg-edge");
    CHECK(group.display_name == "Edge Spawn");
    CHECK(group.area_name == "center");
    CHECK(group.method == "Edge");

    const auto* edge = group.method_config.as_edge();
    REQUIRE(edge != nullptr);
    CHECK(edge->min_number == 2);
    CHECK(edge->max_number == 4);
    CHECK(edge->inset_percent == 150);

    REQUIRE(group.candidates.size() == 2);
    CHECK(group.candidates[0].asset_id == "alpha");
    CHECK(group.candidates[0].weight == doctest::Approx(5.0f));
    CHECK(group.candidates[1].asset_id == "beta");
    CHECK(group.candidates[1].weight == doctest::Approx(7.5f));
}

TEST_CASE("apply_spawn_group_to_json persists area name and quantity") {
    model::SpawnGroup group{};
    group.id = "sg-exact";
    group.display_name = "Exact Spawn";
    group.area_name = "boss_room";
    group.method = "Exact";
    group.method_config = model::MethodConfig::make_exact(3);
    group.candidates.push_back(model::Candidate{"gamma", 12.0f});

    nlohmann::json serialized;
    model::apply_spawn_group_to_json(group, serialized);

    REQUIRE(serialized.is_object());
    CHECK(serialized.value("area", std::string{}) == "boss_room");
    CHECK(serialized.value("position", std::string{}) == "Exact");
    CHECK(serialized.value("quantity", 0) == 3);
    CHECK(serialized.value("min_number", 0) == 3);
    CHECK(serialized.value("max_number", 0) == 3);

    REQUIRE(serialized.contains("candidates"));
    const auto& candidates = serialized.at("candidates");
    REQUIRE(candidates.is_array());
    REQUIRE(candidates.size() == 1);
    CHECK(candidates[0].at("name").get<std::string>() == "gamma");
    CHECK(candidates[0].at("chance").get<float>() == doctest::Approx(12.0f));

    const model::SpawnGroup round_trip = model::spawn_group_from_json(serialized);
    CHECK(round_trip.area_name == group.area_name);
    CHECK(round_trip.method == group.method);
    const auto* exact = round_trip.method_config.as_exact();
    REQUIRE(exact != nullptr);
    CHECK(exact->quantity == 3);
}
