#pragma once

#include <string>

namespace vibble::log {

enum class Level {
    Error = 0,
    Warn  = 1,
    Info  = 2,
    Debug = 3,
};

void set_level(Level level);
Level level();

void reset_time_origin();

void error(const std::string& message);
void warn(const std::string& message);
void info(const std::string& message);
void debug(const std::string& message);

}

