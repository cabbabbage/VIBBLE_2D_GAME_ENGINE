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

    struct Random {
    };

    struct Perimeter {
        int min_number = 2;
        int max_number = 2;
    };

    struct Exact {
        int quantity = 1;
    };

    using Variant = std::variant<None, Random, Perimeter, Exact>;

    MethodConfig() = default;
    explicit MethodConfig(Variant data) : data(std::move(data)) {}

    static MethodConfig make_none() { return MethodConfig{Variant{None{}}}; }

    static MethodConfig make_random() { return MethodConfig{Variant{Random{}}}; }

    static MethodConfig make_perimeter(int min_number = 2, int max_number = 2) {
        if (max_number < min_number) {
            max_number = min_number;
        }
        return MethodConfig{Variant{Perimeter{min_number, max_number}}};
    }

    static MethodConfig make_exact(int quantity = 1) {
        return MethodConfig{Variant{Exact{quantity}}};
    }

    None* as_none() { return std::get_if<None>(&data); }
    const None* as_none() const { return std::get_if<None>(&data); }

    Random* as_random() { return std::get_if<Random>(&data); }
    const Random* as_random() const { return std::get_if<Random>(&data); }

    Perimeter* as_perimeter() { return std::get_if<Perimeter>(&data); }
    const Perimeter* as_perimeter() const { return std::get_if<Perimeter>(&data); }

    Exact* as_exact() { return std::get_if<Exact>(&data); }
    const Exact* as_exact() const { return std::get_if<Exact>(&data); }

    Variant data{None{}};
};

struct SpawnGroup {
    std::string id;
    std::string display_name;
    SpawnMethodId method;
    MethodConfig method_config;
    std::vector<Candidate> candidates;
};

inline void switch_method(SpawnGroup& group, SpawnMethodId method) {
    group.method = std::move(method);
    if (group.method == "Random") {
        group.method_config = MethodConfig::make_random();
    } else if (group.method == "Perimeter") {
        group.method_config = MethodConfig::make_perimeter();
    } else if (group.method == "Exact") {
        group.method_config = MethodConfig::make_exact();
    } else {
        group.method_config = MethodConfig::make_none();
    }
}

}  // namespace vibble::dev_mode::room_config::model
