#pragma once

#include <algorithm>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

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

    struct Edge {
        int min_number = 1;
        int max_number = 1;
        int inset_percent = 100;
};

    struct Exact {
        int quantity = 1;
};

    using Variant = std::variant<None, Random, Perimeter, Edge, Exact>;

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

    static MethodConfig make_edge(int min_number = 1, int max_number = 1, int inset_percent = 100) {
        if (min_number < 1) {
            min_number = 1;
        }
        if (max_number < min_number) {
            max_number = min_number;
        }
        if (inset_percent < 0) inset_percent = 0;
        if (inset_percent > 200) inset_percent = 200;
        return MethodConfig{Variant{Edge{min_number, max_number, inset_percent}}};
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

    Edge* as_edge() { return std::get_if<Edge>(&data); }
    const Edge* as_edge() const { return std::get_if<Edge>(&data); }

    Exact* as_exact() { return std::get_if<Exact>(&data); }
    const Exact* as_exact() const { return std::get_if<Exact>(&data); }

    Variant data{None{}};
};

struct SpawnGroup {
    std::string id;
    std::string display_name;
    std::string area_name;
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
    } else if (group.method == "Edge") {
        group.method_config = MethodConfig::make_edge();
    } else if (group.method == "Exact") {
        group.method_config = MethodConfig::make_exact();
    } else {
        group.method_config = MethodConfig::make_none();
    }
}

namespace detail {

inline std::string read_string(const nlohmann::json& obj, const char* key) {
    if (!obj.is_object()) return {};
    const auto it = obj.find(key);
    if (it == obj.end() || !it->is_string()) return {};
    return it->get<std::string>();
}

inline int read_int(const nlohmann::json& obj, const char* key, int fallback) {
    if (!obj.is_object()) return fallback;
    const auto it = obj.find(key);
    if (it == obj.end()) return fallback;
    if (it->is_number_integer()) return it->get<int>();
    if (it->is_number_float()) return static_cast<int>(it->get<double>());
    if (it->is_string()) {
        try {
            const std::string text = it->get<std::string>();
            size_t consumed = 0;
            const int parsed = std::stoi(text, &consumed);
            if (consumed == text.size()) {
                return parsed;
            }
        } catch (...) {
        }
    }
    return fallback;
}

inline float read_number(const nlohmann::json& value, float fallback) {
    if (value.is_number_float()) return static_cast<float>(value.get<double>());
    if (value.is_number_integer()) return static_cast<float>(value.get<int>());
    if (value.is_string()) {
        try {
            const std::string text = value.get<std::string>();
            size_t consumed = 0;
            const float parsed = std::stof(text, &consumed);
            if (consumed == text.size()) {
                return parsed;
            }
        } catch (...) {
        }
    }
    return fallback;
}

}

inline float read_candidate_weight(const nlohmann::json& candidate) {
    if (!candidate.is_object()) return 0.0f;
    const auto chance_it = candidate.find("chance");
    if (chance_it != candidate.end()) {
        return detail::read_number(*chance_it, 0.0f);
    }
    const auto weight_it = candidate.find("weight");
    if (weight_it != candidate.end()) {
        return detail::read_number(*weight_it, 0.0f);
    }
    return 0.0f;
}

inline SpawnGroup spawn_group_from_json(const nlohmann::json& entry) {
    SpawnGroup group{};
    if (!entry.is_object()) {
        switch_method(group, "Random");
        return group;
    }

    group.id = detail::read_string(entry, "spawn_id");
    group.display_name = detail::read_string(entry, "display_name");
    group.area_name = detail::read_string(entry, "area");

    std::string method = detail::read_string(entry, "position");
    if (method == "Exact Position") {
        method = "Exact";
    }
    if (method.empty()) {
        method = "Random";
    }
    switch_method(group, method);

    if (auto* perimeter = group.method_config.as_perimeter()) {
        const int min_number = detail::read_int(entry, "min_number", perimeter->min_number);
        const int max_number = detail::read_int(entry, "max_number", perimeter->max_number);
        perimeter->min_number = std::max(1, min_number);
        perimeter->max_number = std::max(perimeter->min_number, max_number);
    } else if (auto* edge = group.method_config.as_edge()) {
        const int min_number = detail::read_int(entry, "min_number", edge->min_number);
        const int max_number = detail::read_int(entry, "max_number", edge->max_number);
        const int inset = detail::read_int(entry, "edge_inset_percent", edge->inset_percent);
        edge->min_number = std::max(1, min_number);
        edge->max_number = std::max(edge->min_number, max_number);
        edge->inset_percent = std::clamp(inset, 0, 200);
    } else if (auto* exact = group.method_config.as_exact()) {
        int quantity = detail::read_int(entry, "quantity", detail::read_int(entry, "min_number", exact->quantity));
        if (quantity < 1) quantity = 1;
        exact->quantity = quantity;
    }

    group.candidates.clear();
    const auto it = entry.find("candidates");
    if (it != entry.end() && it->is_array()) {
        for (const auto& candidate : *it) {
            if (!candidate.is_object()) continue;
            Candidate parsed{};
            parsed.asset_id = detail::read_string(candidate, "name");
            parsed.weight = read_candidate_weight(candidate);
            if (!parsed.asset_id.empty() || parsed.weight != 0.0f) {
                group.candidates.push_back(std::move(parsed));
            }
        }
    }

    return group;
}

inline void apply_spawn_group_to_json(const SpawnGroup& group, nlohmann::json& entry) {
    if (!entry.is_object()) {
        entry = nlohmann::json::object();
    }

    entry["spawn_id"] = group.id;
    entry["display_name"] = group.display_name;

    if (!group.area_name.empty()) {
        entry["area"] = group.area_name;
    } else {
        entry.erase("area");
    }

    const std::string method = group.method.empty() ? std::string{"Random"} : group.method;
    entry["position"] = method;

    if (const auto* perimeter = group.method_config.as_perimeter()) {
        entry["min_number"] = perimeter->min_number;
        entry["max_number"] = perimeter->max_number;
        entry.erase("quantity");
        entry.erase("edge_inset_percent");
    } else if (const auto* edge = group.method_config.as_edge()) {
        entry["min_number"] = edge->min_number;
        entry["max_number"] = edge->max_number;
        entry["edge_inset_percent"] = edge->inset_percent;
        entry.erase("quantity");
    } else if (const auto* exact = group.method_config.as_exact()) {
        entry["quantity"] = exact->quantity;
        entry["min_number"] = exact->quantity;
        entry["max_number"] = exact->quantity;
        entry.erase("edge_inset_percent");
    } else {
        entry.erase("quantity");
        entry.erase("edge_inset_percent");
    }

    nlohmann::json candidates = nlohmann::json::array();
    for (const auto& candidate : group.candidates) {
        nlohmann::json candidate_json = nlohmann::json::object();
        candidate_json["name"] = candidate.asset_id;
        candidate_json["chance"] = candidate.weight;
        candidates.push_back(std::move(candidate_json));
    }
    entry["candidates"] = std::move(candidates);
}

}
