#pragma once

#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace vibble::dev_mode::room_config::model {

using SpawnMethodId = std::string;

struct Candidate {
    std::string asset_id;
    float weight = 1.0f;
};

struct MethodConfig {
    struct None {
    };

    struct FixedQuantity {
        int quantity = 1;
    };

    struct WeightedList {
        std::vector<Candidate> candidates;
    };

    using Variant = std::variant<None, FixedQuantity, WeightedList>;

    MethodConfig() = default;
    explicit MethodConfig(Variant data) : data(std::move(data)) {}

    static MethodConfig make_none() { return MethodConfig{Variant{None{}}}; }

    static MethodConfig make_fixed_quantity(int quantity) {
        return MethodConfig{Variant{FixedQuantity{quantity}}};
    }

    static MethodConfig make_weighted_list(std::vector<Candidate> candidates) {
        return MethodConfig{Variant{WeightedList{std::move(candidates)}}};
    }

    const None* as_none() const { return std::get_if<None>(&data); }
    const FixedQuantity* as_fixed_quantity() const { return std::get_if<FixedQuantity>(&data); }
    const WeightedList* as_weighted_list() const { return std::get_if<WeightedList>(&data); }

    Variant data{None{}};
};

struct SpawnGroup {
    std::string id;
    std::string display_name;
    SpawnMethodId method;
    MethodConfig method_config;
    std::vector<Candidate> candidates;
};

}  // namespace vibble::dev_mode::room_config::model
