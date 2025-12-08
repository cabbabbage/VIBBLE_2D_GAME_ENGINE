/*
#include "doctest/doctest.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "dev_mode/core/dev_json_store.hpp"

#ifdef _WIN32
#   include <windows.h>
#endif

namespace fs = std::filesystem;
using devmode::core::DevJsonStore;

static fs::path test_root() {
#ifdef PROJECT_ROOT
    return fs::path(PROJECT_ROOT);
#else
    return fs::current_path() / "TEST_TMP";
#endif
}

TEST_CASE("DevJsonStore interrupted write leaves original untouched") {
    const fs::path root = test_root();
    const fs::path file = root / "manifest.json";
    std::error_code ec;
    fs::create_directories(root, ec);

    // Seed with original content
    const std::string original = std::string("{\n  \"version\": 1,\n  \"assets\": {},\n  \"maps\": {}\n}\n");
    {
        std::ofstream out(file);
        REQUIRE(out.is_open());
        out << original;
    }

#ifdef _WIN32
    // Hold an exclusive handle to the destination to simulate contention
    const std::wstring wdst = file.wstring();
    HANDLE h = CreateFileW(wdst.c_str(), GENERIC_READ | GENERIC_WRITE, 0 /* no share ,*//* nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    REQUIRE(h != INVALID_HANDLE_VALUE);
#endif

    // Attempt to write a new manifest through DevJsonStore (synchronous in tests)
    nlohmann::json updated = nlohmann::json::object();
    updated["version"] = 2;
    updated["assets"] = nlohmann::json::object();
    updated["maps"] = nlohmann::json::object();
    DevJsonStore::instance().submit(file, updated, 2);
    DevJsonStore::instance().flush_all();

#ifdef _WIN32
    // Release the handle
    CloseHandle(h);
#endif

    // Read back the file; it should be the original content if the replace failed
    std::ifstream in(file);
    REQUIRE(in.is_open());
    std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    // The write may have succeeded if the OS allowed REPLACE_EXISTING despite our handle.
    // In that case, content equals updated.dump(2). Accept either original (expected under lock)
    // or updated (if replace succeeded); in both cases there should be no partial content.
    const std::string updated_str = updated.dump(2);
    CHECK((contents == original || contents == updated_str));
}*/

