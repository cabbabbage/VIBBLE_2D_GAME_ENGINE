#include "dev_ui_settings.hpp"

#include "dev_mode/core/dev_json_store.hpp"

#include <filesystem>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace devmode::ui_settings {

namespace {

std::mutex& settings_mutex() {
    static std::mutex mutex;
    return mutex;
}

nlohmann::json& settings_cache() {
    static nlohmann::json cache = nlohmann::json::object();
    return cache;
}

bool& settings_loaded_flag() {
    static bool loaded = false;
    return loaded;
}

bool& settings_dirty_flag() {
    static bool dirty = false;
    return dirty;
}

std::filesystem::path settings_path() {
    return std::filesystem::path("dev_mode_settings.json");
}

void ensure_loaded() {
    if (settings_loaded_flag()) {
        return;
    }
    settings_loaded_flag() = true;

    auto loaded = devmode::core::DevJsonStore::instance().load(settings_path());
    if (!loaded.is_object()) {
        loaded = nlohmann::json::object();
    }
    settings_cache() = std::move(loaded);
    settings_dirty_flag() = false;
}

std::vector<std::string> split_key(std::string_view key) {
    std::vector<std::string> parts;
    std::string current;
    for (char ch : key) {
        if (ch == '.') {
            if (!current.empty()) {
                parts.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty()) {
        parts.push_back(current);
    }
    return parts;
}

}

bool load_bool(std::string_view key, bool default_value) {
    if (key.empty()) {
        return default_value;
    }
    std::lock_guard<std::mutex> lock(settings_mutex());
    ensure_loaded();

    const auto parts = split_key(key);
    if (parts.empty()) {
        return default_value;
    }

    const nlohmann::json* node = &settings_cache();
    for (const auto& part : parts) {
        if (!node->is_object()) {
            return default_value;
        }
        auto it = node->find(part);
        if (it == node->end()) {
            return default_value;
        }
        node = &(*it);
    }

    if (!node->is_boolean()) {
        return default_value;
    }
    return node->get<bool>();
}

void save_bool(std::string_view key, bool value) {
    if (key.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(settings_mutex());
    ensure_loaded();

    const auto parts = split_key(key);
    if (parts.empty()) {
        return;
    }

    nlohmann::json* node = &settings_cache();
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        nlohmann::json& next = (*node)[parts[i]];
        if (!next.is_object()) {
            next = nlohmann::json::object();
        }
        node = &next;
    }
    (*node)[parts.back()] = value;
    settings_dirty_flag() = true;
    devmode::core::DevJsonStore::instance().submit(settings_path(), settings_cache(), 4);
}

double load_number(std::string_view key, double default_value) {
    if (key.empty()) {
        return default_value;
    }
    std::lock_guard<std::mutex> lock(settings_mutex());
    ensure_loaded();

    const auto parts = split_key(key);
    if (parts.empty()) {
        return default_value;
    }

    const nlohmann::json* node = &settings_cache();
    for (const auto& part : parts) {
        if (!node->is_object()) {
            return default_value;
        }
        auto it = node->find(part);
        if (it == node->end()) {
            return default_value;
        }
        node = &(*it);
    }

    try {
        if (node->is_number_float()) {
            return node->get<double>();
        }
        if (node->is_number_integer()) {
            return static_cast<double>(node->get<int64_t>());
        }
        if (node->is_string()) {
            const std::string text = node->get<std::string>();
            size_t idx = 0;
            double parsed = std::stod(text, &idx);
            if (idx == text.size()) {
                return parsed;
            }
        }
    } catch (...) {
    }
    return default_value;
}

void save_number(std::string_view key, double value) {
    if (key.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(settings_mutex());
    ensure_loaded();

    const auto parts = split_key(key);
    if (parts.empty()) {
        return;
    }

    nlohmann::json* node = &settings_cache();
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        nlohmann::json& next = (*node)[parts[i]];
        if (!next.is_object()) {
            next = nlohmann::json::object();
        }
        node = &next;
    }
    (*node)[parts.back()] = value;
    settings_dirty_flag() = true;
    devmode::core::DevJsonStore::instance().submit(settings_path(), settings_cache(), 4);
}

}
