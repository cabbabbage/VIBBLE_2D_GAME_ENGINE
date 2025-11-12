#include <algorithm>
#include <cstdarg>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "ENGINE/dev_mode/asset_sections/animation_editor_window/AnimationDocument.hpp"

// Minimal stub so AnimationDocument.cpp links without SDL2.
extern "C" void SDL_Log(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    std::fputc('\n', stderr);
    va_end(args);
}

using animation_editor::AnimationDocument;

namespace {

nlohmann::json build_asset_fixture() {
    return {
        {"start", "parent_walk"},
        {"animations",
         {
             {"parent_walk",
              {
                  {"loop", true},
                  {"number_of_frames", 6},
                  {"source",
                   {
                       {"kind", "folder"},
                       {"path", "parent_walk"},
                       {"name", ""},
                   }},
              }},
             {"child_walk",
              {
                  {"inherit_source_movement", true},
                  {"on_end", "parent_walk"},
                  {"source",
                   {
                       {"kind", "animation"},
                       {"name", "parent_walk"},
                       {"path", ""},
                   }},
              }},
             {"child_walk_fallback",
              {
                  {"loop", true},
                  {"source",
                   {
                       {"kind", "animation"},
                       {"name", ""},
                       {"path", "parent_walk"},
                   }},
              }},
         }},
    };
}

nlohmann::json payload_or_throw(AnimationDocument& doc, const std::string& animation_id) {
    auto payload = doc.animation_payload(animation_id);
    if (!payload) {
        throw std::runtime_error("missing payload for " + animation_id);
    }
    return nlohmann::json::parse(*payload, nullptr, true);
}

void report_payload(const std::string& name, const nlohmann::json& payload) {
    std::cout << name << ": source.kind=" << payload["source"].value("kind", "<missing>")
              << ", source.name='" << payload["source"].value("name", "<missing>")
              << "', source.path='" << payload["source"].value("path", "<missing>")
              << "', on_end='" << payload.value("on_end", "<missing>") << "'\n";
}

}  // namespace

#ifdef main
#undef main
#endif

int main() {
    std::filesystem::path asset_root = "SRC/assets/__qa_fixture__";

    nlohmann::json persisted;
    auto persist = [&](const nlohmann::json& updated) { persisted = updated; };

    AnimationDocument doc;
    doc.load_from_manifest(build_asset_fixture(), asset_root, persist);

    doc.rename_animation("parent_walk", "parent_walk_new");

    bool ok = true;

    auto ids = doc.animation_ids();
    bool has_new = std::find(ids.begin(), ids.end(), "parent_walk_new") != ids.end();
    bool lacks_old = std::find(ids.begin(), ids.end(), "parent_walk") == ids.end();
    std::cout << "animation_ids -> contains parent_walk_new=" << (has_new ? "true" : "false")
              << ", missing parent_walk=" << (lacks_old ? "true" : "false") << "\n";
    ok &= has_new && lacks_old;

    auto start = doc.start_animation();
    std::cout << "start animation -> " << (start ? *start : "<null>") << "\n";
    ok &= start.has_value() && *start == "parent_walk_new";

    auto parent_payload = payload_or_throw(doc, "parent_walk_new");
    report_payload("parent_walk_new", parent_payload);

    auto child_payload = payload_or_throw(doc, "child_walk");
    report_payload("child_walk", child_payload);
    ok &= child_payload["source"].value("name", "") == "parent_walk_new";
    ok &= child_payload.value("on_end", "") == "parent_walk_new";

    auto fallback_payload = payload_or_throw(doc, "child_walk_fallback");
    report_payload("child_walk_fallback", fallback_payload);
    ok &= fallback_payload["source"].value("path", "") == "parent_walk_new";

    doc.save_to_file();
    if (persisted.empty()) {
        std::cerr << "persist callback did not fire\n";
        return 1;
    }

    AnimationDocument reloaded;
    reloaded.load_from_manifest(persisted, asset_root, {});

    auto re_start = reloaded.start_animation();
    std::cout << "reload start animation -> " << (re_start ? *re_start : "<null>") << "\n";

    auto re_child = payload_or_throw(reloaded, "child_walk");
    report_payload("reloaded child_walk", re_child);

    auto re_fallback = payload_or_throw(reloaded, "child_walk_fallback");
    report_payload("reloaded child_walk_fallback", re_fallback);
    ok &= re_child["source"].value("name", "") == "parent_walk_new";
    ok &= re_child.value("on_end", "") == "parent_walk_new";
    ok &= re_fallback["source"].value("path", "") == "parent_walk_new";
    ok &= re_start.has_value() && *re_start == "parent_walk_new";

    if (!ok) {
        std::cerr << "Animation rename QA checks failed.\n";
        return 2;
    }

    std::cout << "Animation rename QA checks passed.\n";
    return 0;
}
