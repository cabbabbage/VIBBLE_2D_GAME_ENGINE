#include "doctest/doctest.h"

#include <filesystem>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include "dev_mode/dev_controls_persistence.hpp"

TEST_CASE("persist_map_info_to_disk reports failures for unwritable path") {
    namespace fs = std::filesystem;

    const fs::path temp_dir = fs::temp_directory_path() / "vibble_unwritable_map_info";
    fs::create_directories(temp_dir);

    const fs::path map_info_path = temp_dir; // intentionally a directory path

    nlohmann::json payload = {
        {"name", "test_map"},
        {"version", 1}
    };

    std::ostringstream log;
    const bool result = devmode::write_map_info_json(map_info_path.string(), payload, log);

    CHECK_FALSE(result);
    CHECK(log.str().find("Failed to open") != std::string::npos);

    fs::remove_all(temp_dir);
}
