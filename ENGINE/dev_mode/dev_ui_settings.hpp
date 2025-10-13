#pragma once

#include <string_view>

namespace devmode::ui_settings {

bool load_bool(std::string_view key, bool default_value);
void save_bool(std::string_view key, bool value);

}
