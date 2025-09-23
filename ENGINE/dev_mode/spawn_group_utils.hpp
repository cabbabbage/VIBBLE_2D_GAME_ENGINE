#pragma once

#include <string>

#include <nlohmann/json_fwd.hpp>

namespace devmode::spawn {

// Generate a unique spawn identifier using the historical "spn-" prefix.
std::string generate_spawn_id();

// Ensure that the provided JSON object contains a `spawn_groups` array and
// return a reference to it. Legacy payloads that store the array under the
// `assets` key are migrated transparently.
nlohmann::json& ensure_spawn_groups_array(nlohmann::json& root);

// Return a pointer to the `spawn_groups` array if present (handling the legacy
// `assets` key as well). Returns nullptr if no array could be resolved.
const nlohmann::json* find_spawn_groups_array(const nlohmann::json& root);

// Normalise perimeter spawn groups so that the minimum/maximum quantities are
// valid. Returns true when the payload was modified.
bool sanitize_perimeter_spawn_groups(nlohmann::json& groups);

} // namespace devmode::spawn

