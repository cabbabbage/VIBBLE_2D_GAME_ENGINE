#include "log.hpp"

#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <mutex>
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
    os << '[' << level_tag(level) << "] +" << std::fixed << std::setprecision(3) << secs
       << "s: " << message << '\n';
    os.flush();
}

} // namespace

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

} // namespace vibble::log

