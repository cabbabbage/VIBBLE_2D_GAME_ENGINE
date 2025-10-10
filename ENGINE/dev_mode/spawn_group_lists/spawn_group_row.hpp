#pragma once

#include <SDL.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

// SpawnGroupRow acts as a light-weight data model that mirrors the spawn group
// entry in the underlying JSON array. The row exposes a handful of optional
// presentation properties used by the UI widgets, but does not own any heavy
// view logic.
class SpawnGroupRow {
public:
    SpawnGroupRow();
    explicit SpawnGroupRow(nlohmann::json* entry);

    void bind(nlohmann::json* entry);
    void set_shadow_entry(const nlohmann::json& entry);

    nlohmann::json* mutable_entry();
    const nlohmann::json* mutable_entry() const;
    const nlohmann::json& entry_view() const;

    std::string spawn_id() const;

    void set_ownership_label(const std::string& label, SDL_Color color);
    void clear_ownership_label();

    void set_area_names_provider(std::function<std::vector<std::string>()> provider);
    const std::function<std::vector<std::string>()>& area_names_provider() const;

    void set_stack_key(std::string key);
    const std::optional<std::string>& stack_key() const;

    void lock_method_to(std::string method);
    const std::optional<std::string>& method_lock() const;
    void clear_method_lock();

    void set_quantity_hidden(bool hidden);
    bool quantity_hidden() const;

    const std::string& ownership_label() const;
    std::optional<SDL_Color> ownership_color() const;

private:
    nlohmann::json* entry_ = nullptr;
    nlohmann::json shadow_entry_;
    std::string ownership_label_{};
    std::optional<SDL_Color> ownership_color_{};
    std::function<std::vector<std::string>()> area_provider_{};
    std::optional<std::string> stack_key_{};
    std::optional<std::string> method_lock_{};
    bool quantity_hidden_ = false;
};

