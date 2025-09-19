#pragma once

#include <filesystem>
#include <initializer_list>
#include <string>
#include <system_error>

namespace ui_fonts {

inline std::string resolve_font_path(std::initializer_list<const char*> candidates) {
    const char* fallback = nullptr;
    for (const char* path : candidates) {
        if (!path || !*path) continue;
        if (!fallback) fallback = path;
        std::error_code ec;
        if (std::filesystem::exists(path, ec) && !ec) {
            return std::string(path);
        }
    }
    return fallback ? std::string(fallback) : std::string{};
}

inline std::string decorative_bold() {
#ifdef _WIN32
    return resolve_font_path({
        "C:/Windows/Fonts/COPRGTB.TTF",
        "C:/Windows/Fonts/Impact.ttf",
        "C:/Windows/Fonts/segoeuib.ttf",
        "C:/Windows/Fonts/arialbd.ttf"
    });
#else
    return resolve_font_path({
        "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSerifBold.ttf"
    });
#endif
}

inline std::string serif_regular() {
#ifdef _WIN32
    return resolve_font_path({
        "C:/Windows/Fonts/GOUDOS.TTF",
        "C:/Windows/Fonts/georgia.ttf",
        "C:/Windows/Fonts/times.ttf",
        "C:/Windows/Fonts/segoeui.ttf"
    });
#else
    return resolve_font_path({
        "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSerif.ttf"
    });
#endif
}

inline std::string serif_italic() {
#ifdef _WIN32
    return resolve_font_path({
        "C:/Windows/Fonts/GOUDOSI.TTF",
        "C:/Windows/Fonts/georgiai.ttf",
        "C:/Windows/Fonts/timesi.ttf",
        "C:/Windows/Fonts/segoeuii.ttf"
    });
#else
    return resolve_font_path({
        "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Italic.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSerif-Italic.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSerifItalic.ttf"
    });
#endif
}

inline std::string sans_regular() {
#ifdef _WIN32
    return resolve_font_path({
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/verdana.ttf"
    });
#else
    return resolve_font_path({
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSans.ttf"
    });
#endif
}

inline std::string monospace() {
#ifdef _WIN32
    return resolve_font_path({
        "C:/Windows/Fonts/consola.ttf",
        "C:/Windows/Fonts/Courier.ttf",
        "C:/Windows/Fonts/lucon.ttf"
    });
#else
    return resolve_font_path({
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
        "/usr/share/fonts/truetype/freefont/FreeMono.ttf"
    });
#endif
}

} // namespace ui_fonts
