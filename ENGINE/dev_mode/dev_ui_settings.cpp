#include "dev_ui_settings.hpp"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>
#include <iostream>

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

    auto& cache = settings_cache();
    std::ifstream in(settings_path());
    if (!in.is_open()) {
        cache = nlohmann::json::object();
        return;
    }
    try {
        in >> cache;
        if (!cache.is_object()) {
            cache = nlohmann::json::object();
        }
    } catch (...) {
        cache = nlohmann::json::object();
    }
}

void persist_if_dirty() {
    if (!settings_dirty_flag()) {
        return;
    }
    settings_dirty_flag() = false;
    const auto path = settings_path();
    std::ofstream out(path);
    if (!out.is_open()) {
        std::cerr << "[dev_ui_settings] Failed to open '" << path << "' for writing\n";
        return;
    }
    try {
        out << settings_cache().dump(4);
    } catch (const std::exception& ex) {
        std::cerr << "[dev_ui_settings] Failed to write settings: " << ex.what() << "\n";
    }
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

} // namespace

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
    persist_if_dirty();
}

} // namespace devmode::ui_settings
