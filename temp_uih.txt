#pragma once

#include <string_view>

namespace devmode::ui_settings {

bool load_bool(std::string_view key, bool default_value);
void save_bool(std::string_view key, bool value);
double load_number(std::string_view key, double default_value);
void save_number(std::string_view key, double value);

}
