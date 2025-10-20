#pragma once

#include <string>

namespace vibble::log {

enum class Level {
    Error = 0,
    Warn  = 1,
    Info  = 2,
    Debug = 3,
};

// Configure at runtime via env var `VIBBLE_LOG_LEVEL` (error|warn|info|debug)
void set_level(Level level);
Level level();

// Optional: set the origin for relative timestamps
void reset_time_origin();

// Simple helpers for string-based logging
void error(const std::string& message);
void warn(const std::string& message);
void info(const std::string& message);
void debug(const std::string& message);

}  // namespace vibble::log

