#include "log.hpp"

#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>

namespace {

std::mutex& log_mutex() {
    static std::mutex m;
    return m;
}

vibble::log::Level& global_level() {
    static vibble::log::Level lvl = vibble::log::Level::Info;
    return lvl;
}

std::atomic<bool>& env_init_flag() {
    static std::atomic<bool> f{false};
    return f;
}

std::unique_ptr<std::ofstream>& file_sink() {
    static std::unique_ptr<std::ofstream> f{};
    return f;
}

bool& append_mode_flag() {
    static bool append = false;
    return append;
}

std::chrono::steady_clock::time_point& time_origin() {
    static auto t0 = std::chrono::steady_clock::now();
    return t0;
}

vibble::log::Level parse_level_env(std::string_view v) {
    auto lower = std::string(v);
    for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower == "error") return vibble::log::Level::Error;
    if (lower == "warn" || lower == "warning") return vibble::log::Level::Warn;
    if (lower == "info") return vibble::log::Level::Info;
    if (lower == "debug") return vibble::log::Level::Debug;
    return vibble::log::Level::Info;
}

void init_from_env_once() {
    bool expected = false;
    if (!env_init_flag().compare_exchange_strong(expected, true)) {
        return;
    }
    if (const char* v = std::getenv("VIBBLE_LOG_LEVEL")) {
        global_level() = parse_level_env(v);
    }

    const char* file = std::getenv("VIBBLE_LOG_FILE");
    if (file && *file) {
        const char* append = std::getenv("VIBBLE_LOG_APPEND");
        append_mode_flag() = (append && (*append == '1' || *append == 'y' || *append == 'Y' || *append == 't' || *append == 'T'));
        std::ios_base::openmode mode = std::ios::out;
        if (append_mode_flag()) mode |= std::ios::app; else mode |= std::ios::trunc;
        auto ofs = std::make_unique<std::ofstream>(file, mode);
        if (ofs->good()) {
            file_sink() = std::move(ofs);
        }
    }
}

const char* level_tag(vibble::log::Level level) {
    switch (level) {
        case vibble::log::Level::Error: return "ERROR";
        case vibble::log::Level::Warn:  return "WARN";
        case vibble::log::Level::Info:  return "INFO";
        case vibble::log::Level::Debug: return "DEBUG";
        default:                return "INFO";
    }
}

void log_line_impl(vibble::log::Level level, const std::string& message) {
    init_from_env_once();
    if (static_cast<int>(level) > static_cast<int>(global_level())) {
        return;
    }
    using namespace std::chrono;
    const auto now = steady_clock::now();
    const double secs = duration_cast<duration<double>>(now - time_origin()).count();
    std::lock_guard<std::mutex> lock(log_mutex());
    std::ostream& os = (level == vibble::log::Level::Error) ? std::cerr : std::cout;
    const std::string line = std::string("[") + level_tag(level) + "] +" +
        [&]() { std::ostringstream ss; ss.setf(std::ios::fixed); ss << std::setprecision(3) << secs; return ss.str(); }() +
        "s: " + message + '\n';
    os << line;
    os.flush();
    if (file_sink()) {
        (*file_sink()) << line;
        file_sink()->flush();
    }
}

}

namespace vibble::log {

void set_level(Level level) {
    std::lock_guard<std::mutex> lock(log_mutex());
    global_level() = level;
}

Level level() {
    init_from_env_once();
    return global_level();
}

void reset_time_origin() {
    std::lock_guard<std::mutex> lock(log_mutex());
    time_origin() = std::chrono::steady_clock::now();
}

void error(const std::string& message) { log_line_impl(Level::Error, message); }
void warn (const std::string& message) { log_line_impl(Level::Warn,  message); }
void info (const std::string& message) { log_line_impl(Level::Info,  message); }
void debug(const std::string& message) { log_line_impl(Level::Debug, message); }

}
