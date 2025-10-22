// SkyMapFetcher.cpp
#include "SkyMapFetcher.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <regex>
#include <vector>
#include <stdexcept>
#include <iostream>
#include <sstream>
#include <string>
#include <cctype>
#include <random>
#include <system_error>
#include <cstdint>
#include <utility>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

// stb_image and stb_image_write (public domain / MIT-like).
// Embedded here so you do not need extra libs for PNG output.
#define STB_IMAGE_IMPLEMENTATION
#include <stdint.h>
extern "C" {
#include "stb_image.h"
}
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

using json = nlohmann::json;

namespace {

inline float clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

inline int clamp_int(int value, int min_value, int max_value) {
    return std::min(std::max(value, min_value), max_value);
}

// Fallback/default for line smoothing strength if not defined in the header;
// pick a conservative default (0.0 means no additional smoothing).
// Provide fallback defaults for contrast and hash strength as well so code
// that compares cached metadata against these constants compiles correctly.
static constexpr double kSkyContrastFactor = 2.0;
static constexpr double kSkyHashStrength = 0.0;
static constexpr double kSkyLineSmoothStrength = 0.0;

struct ImageData {
    int width  = 0;
    int height = 0;
    std::vector<unsigned char> pixels;
};

bool load_png_rgba(const std::filesystem::path& path, ImageData& out_image) {
    int w = 0;
    int h = 0;
    int comp = 0;
    const std::string path_str = path.string();
    stbi_uc* data = stbi_load(path_str.c_str(), &w, &h, &comp, 4);
    if (!data) {
        return false;
    }

    const size_t total = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
    out_image.width  = w;
    out_image.height = h;
    out_image.pixels.assign(data, data + total);
    stbi_image_free(data);
    return true;
}

void resample_image_bilinear(const ImageData& src, int dst_width, int dst_height, std::vector<unsigned char>& out_pixels) {
    if (dst_width <= 0 || dst_height <= 0 || src.width <= 0 || src.height <= 0 || src.pixels.empty()) {
        out_pixels.clear();
        return;
    }

    out_pixels.resize(static_cast<size_t>(dst_width) * static_cast<size_t>(dst_height) * 4);
    const float scale_x = static_cast<float>(src.width) / static_cast<float>(dst_width);
    const float scale_y = static_cast<float>(src.height) / static_cast<float>(dst_height);

    for (int y = 0; y < dst_height; ++y) {
        const float src_y = (static_cast<float>(y) + 0.5f) * scale_y - 0.5f;
        const int y0 = clamp_int(static_cast<int>(std::floor(src_y)), 0, src.height - 1);
        const int y1 = clamp_int(y0 + 1, 0, src.height - 1);
        const float fy = clamp01(src_y - static_cast<float>(y0));

        for (int x = 0; x < dst_width; ++x) {
            const float src_x = (static_cast<float>(x) + 0.5f) * scale_x - 0.5f;
            const int x0 = clamp_int(static_cast<int>(std::floor(src_x)), 0, src.width - 1);
            const int x1 = clamp_int(x0 + 1, 0, src.width - 1);
            const float fx = clamp01(src_x - static_cast<float>(x0));

            const size_t idx_dst = static_cast<size_t>(y) * static_cast<size_t>(dst_width) * 4 + static_cast<size_t>(x) * 4;

            for (int channel = 0; channel < 4; ++channel) {
                const size_t idx00 = (static_cast<size_t>(y0) * static_cast<size_t>(src.width) + static_cast<size_t>(x0)) * 4 + static_cast<size_t>(channel);
                const size_t idx10 = (static_cast<size_t>(y0) * static_cast<size_t>(src.width) + static_cast<size_t>(x1)) * 4 + static_cast<size_t>(channel);
                const size_t idx01 = (static_cast<size_t>(y1) * static_cast<size_t>(src.width) + static_cast<size_t>(x0)) * 4 + static_cast<size_t>(channel);
                const size_t idx11 = (static_cast<size_t>(y1) * static_cast<size_t>(src.width) + static_cast<size_t>(x1)) * 4 + static_cast<size_t>(channel);

                const float c00 = static_cast<float>(src.pixels[idx00]);
                const float c10 = static_cast<float>(src.pixels[idx10]);
                const float c01 = static_cast<float>(src.pixels[idx01]);
                const float c11 = static_cast<float>(src.pixels[idx11]);

                const float c0 = c00 + (c10 - c00) * fx;
                const float c1 = c01 + (c11 - c01) * fx;
                const float c = c0 + (c1 - c0) * fy;

                out_pixels[idx_dst + channel] = static_cast<unsigned char>(std::round(clamp01(c / 255.0f) * 255.0f));
            }
        }
    }
}

void blend_overlay_texture(std::vector<unsigned char>& base_pixels,
                           const std::vector<unsigned char>& overlay_pixels,
                           int width,
                           int height) {
    if (width <= 0 || height <= 0 || base_pixels.empty() || overlay_pixels.empty()) {
        return;
    }

    const size_t total_pixels = static_cast<size_t>(width) * static_cast<size_t>(height);
    for (size_t i = 0; i < total_pixels; ++i) {
        const size_t idx = i * 4;

        const float r = static_cast<float>(overlay_pixels[idx + 0]) / 255.0f;
        const float g = static_cast<float>(overlay_pixels[idx + 1]) / 255.0f;
        const float b = static_cast<float>(overlay_pixels[idx + 2]) / 255.0f;
        const float a = static_cast<float>(overlay_pixels[idx + 3]) / 255.0f;

        const float luminance = 1.0f - clamp01(0.2126f * r + 0.7152f * g + 0.0722f * b);
        // Fully invert the overlay luminance so bright overlay pixels produce full opacity.
        constexpr float overlay_strength = 0.50f;
        float overlay_alpha = clamp01(a * luminance * overlay_strength);

        if (overlay_alpha <= 0.0f) {
            continue;
        }

        for (int channel = 0; channel < 3; ++channel) {
            const float overlay_c = static_cast<float>(overlay_pixels[idx + channel]) / 255.0f;
            const float base_c = static_cast<float>(base_pixels[idx + channel]) / 255.0f;
            const float blended = clamp01(base_c * (1.0f - overlay_alpha) + overlay_c * overlay_alpha);
            base_pixels[idx + channel] = static_cast<unsigned char>(std::round(blended * 255.0f));
        }
    }
}

bool load_random_overlay_from_directory(std::vector<unsigned char>& out_pixels,
                                        int width,
                                        int height,
                                        const std::filesystem::path& overlay_dir,
                                        std::mt19937& rng) {
    out_pixels.clear();
    if (!std::filesystem::exists(overlay_dir) || !std::filesystem::is_directory(overlay_dir)) {
        return false;
    }

    std::vector<std::filesystem::path> png_files;
    std::error_code dir_ec;
    const std::filesystem::directory_iterator end_iter;
    for (std::filesystem::directory_iterator it(overlay_dir, dir_ec); !dir_ec && it != end_iter; ++it) {
        const auto& entry = *it;
        if (!entry.is_regular_file()) {
            continue;
        }
        const auto& path = entry.path();
        if (path.extension() == ".png" || path.extension() == ".PNG") {
            png_files.push_back(path);
        }
    }

    if (dir_ec || png_files.empty()) {
        return false;
    }

    std::uniform_int_distribution<size_t> dist(0, png_files.size() - 1);
    const auto& selected_path = png_files[dist(rng)];

    ImageData overlay_image;
    if (!load_png_rgba(selected_path, overlay_image)) {
        return false;
    }

    std::vector<unsigned char> scaled_overlay;
    resample_image_bilinear(overlay_image, width, height, scaled_overlay);
    if (scaled_overlay.empty()) {
        return false;
    }

    out_pixels = std::move(scaled_overlay);
    return true;
}

void overlay_random_texture(std::vector<unsigned char>& base_pixels, int width, int height) {
    if (width <= 0 || height <= 0 || base_pixels.empty()) {
        return;
    }

    std::mt19937 rng;
    try {
        std::random_device rd;
        rng.seed(rd());
    } catch (...) {
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        rng.seed(static_cast<unsigned int>(now.count()));
    }

    const std::filesystem::path esoteric_dir{"SRC/misc_content/overlay_options_esoteric"};
    const std::filesystem::path americana_dir{"SRC/misc_content/overlay_options_americana"};

    std::vector<unsigned char> overlay_esoteric;
    std::vector<unsigned char> overlay_americana;

    const bool has_esoteric = load_random_overlay_from_directory(overlay_esoteric, width, height, esoteric_dir, rng);
    const bool has_americana = load_random_overlay_from_directory(overlay_americana, width, height, americana_dir, rng);

    if (!has_esoteric && !has_americana) {
        return;
    }

    std::vector<unsigned char> merged(static_cast<size_t>(width) * static_cast<size_t>(height) * 4, 0u);

    if (has_esoteric && has_americana) {
        const size_t total_pixels = static_cast<size_t>(width) * static_cast<size_t>(height);
        for (size_t i = 0; i < total_pixels; ++i) {
            const size_t idx = i * 4;
            for (int channel = 0; channel < 4; ++channel) {
                const int value = static_cast<int>(overlay_esoteric[idx + channel]) +
                                  static_cast<int>(overlay_americana[idx + channel]);
                merged[idx + channel] = static_cast<unsigned char>(value / 2);
            }
        }
    } else if (has_esoteric) {
        merged = std::move(overlay_esoteric);
    } else {
        merged = std::move(overlay_americana);
    }

    const size_t total_pixels = static_cast<size_t>(width) * static_cast<size_t>(height);
    for (size_t i = 0; i < total_pixels; ++i) {
        const size_t idx = i * 4;
        for (int channel = 0; channel < 3; ++channel) {
            merged[idx + channel] = static_cast<unsigned char>(255 - merged[idx + channel]);
        }
    }

    blend_overlay_texture(base_pixels, merged, width, height);
}

// Push the sky texture to an extreme black/white mask so any downstream blur
// works from high-contrast input. Every pixel becomes either fully black or
// fully white based on its luminance.
void apply_harsh_contrast(std::vector<unsigned char>& pixels, int width, int height) {
    if (width <= 0 || height <= 0 || pixels.empty()) {
        return;
    }

    constexpr float contrast_factor = 2.2f;
    constexpr float brightness_offset = 0.05f;
    constexpr float midpoint = 0.5f;
    constexpr float binary_threshold = 0.5f;

    const size_t total_pixels = static_cast<size_t>(width) * static_cast<size_t>(height);
    for (size_t i = 0; i < total_pixels; ++i) {
        const size_t idx = i * 4;

        const float r = static_cast<float>(pixels[idx + 0]) / 255.0f;
        const float g = static_cast<float>(pixels[idx + 1]) / 255.0f;
        const float b = static_cast<float>(pixels[idx + 2]) / 255.0f;

        float luminance = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        luminance = (luminance - midpoint) * contrast_factor + midpoint;
        luminance = clamp01(luminance + brightness_offset);

        const float binary_value = (luminance >= binary_threshold) ? 1.0f : 0.0f;
        const unsigned char channel_value = static_cast<unsigned char>(binary_value * 255.0f);

        for (int channel = 0; channel < 3; ++channel) {
            pixels[idx + channel] = channel_value;
        }
    }
}

void apply_sky_map_post_fx(std::vector<unsigned char>& pixels, int width, int height) {
    apply_harsh_contrast(pixels, width, height);
}

} // namespace

static size_t CurlWriteToString(void* contents, size_t size, size_t nmemb, void* userp) {
    const size_t realSize = size * nmemb;
    std::string* s = static_cast<std::string*>(userp);
    s->append(static_cast<const char*>(contents), realSize);
    return realSize;
}

bool SkyMapFetcher::http_get_text(const std::string& url, std::string& out_text, long& http_code) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "SkyMapFetcher/1.0");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out_text);
    CURLcode res = curl_easy_perform(curl);
    bool ok = (res == CURLE_OK);
    if (ok) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);
    return ok && (http_code >= 200 && http_code < 300);
}

bool SkyMapFetcher::http_get_binary(const std::string& url, std::string& out_bytes, long& http_code) {
    // Same as text; we keep raw bytes in std::string.
    return http_get_text(url, out_bytes, http_code);
}

bool SkyMapFetcher::geolocate_ip(double& lat_deg, double& lon_deg, std::string& city) {
    // Try ip-api first (no key). Falls back to ipapi.co.
    {
        std::string body; long code = 0;
        if (http_get_text("http://ip-api.com/json", body, code)) {
            try {
                auto j = json::parse(body);
                if (j.value("status", std::string()) == "success") {
                    lat_deg = j.value("lat", 0.0);
                    lon_deg = j.value("lon", 0.0);
                    city = j.value("city", std::string());
                    return true;
                }
            } catch (...) {}
        }
    }
    {
        std::string body; long code = 0;
        if (http_get_text("https://ipapi.co/json/", body, code)) {
            try {
                auto j = json::parse(body);
                lat_deg = j.value("latitude", 0.0);
                lon_deg = j.value("longitude", 0.0);
                city = j.value("city", std::string());
                if (lat_deg != 0.0 || lon_deg != 0.0) return true;
            } catch (...) {}
        }
    }
    return false;
}

double SkyMapFetcher::julian_date(std::time_t t) {
    // Convert Unix time (UTC) to Julian Date.
    // JD of Unix epoch (1970-01-01 00:00:00 UTC) is 2440587.5
    return 2440587.5 + static_cast<double>(t) / 86400.0;
}

double SkyMapFetcher::gmst_deg(double jd) {
    // Approx GMST in degrees. Good enough for centering a several-degree cutout.
    // GMST = 280.46061837 + 360.98564736629 * (JD - 2451545.0)
    constexpr double JD2000 = 2451545.0;
    double gmst = 280.46061837 + 360.98564736629 * (jd - JD2000);
    // Normalize to [0,360)
    gmst = fmod(gmst, 360.0);
    if (gmst < 0) gmst += 360.0;
    return gmst;
}

std::pair<double,double> SkyMapFetcher::zenith_radec(double latitude_deg,
                                                     double longitude_deg,
                                                     std::time_t utc_seconds_since_epoch) {
    if (utc_seconds_since_epoch == 0) {
        utc_seconds_since_epoch = std::time(nullptr);
    }
    const double jd = julian_date(utc_seconds_since_epoch);
    double lst = gmst_deg(jd) + longitude_deg; // degrees
    lst = fmod(lst, 360.0);
    if (lst < 0) lst += 360.0;

    double ra_deg = lst;           // RA at zenith ~ LST
    double dec_deg = latitude_deg; // Dec at zenith ~ latitude
    return {ra_deg, dec_deg};
}

std::string SkyMapFetcher::build_skyview_url(double ra_deg, double dec_deg,
                                             int pixels, double fov_deg,
                                             const std::string& survey) {
    // SkyView runquery parameters:
    // Position=RA,Dec  coordinates=J2000  pixels=N  size=FOVdeg
    // projection=Tan scaling=Linear survey=<survey>
    // The response is an HTML page that contains a "quick look jpeg" link.
    char buf[2048];
    // Survey must have spaces encoded as '+'
    std::string survey_enc = survey;
    for (auto& ch : survey_enc) if (ch == ' ') ch = '+';

    std::snprintf(buf, sizeof(buf),
        "https://skyview.gsfc.nasa.gov/current/cgi/runquery.pl?"
        "Position=%.8f,%.8f&coordinates=J2000&pixels=%d&size=%.4f&projection=Tan&scaling=Linear&survey=%s",
        ra_deg, dec_deg, pixels, fov_deg, survey_enc.c_str());
    return std::string(buf);
}

namespace {

std::string trim_left(const std::string& value) {
    std::size_t pos = 0;
    while (pos < value.size() && std::isspace(static_cast<unsigned char>(value[pos]))) {
        ++pos;
    }
    return value.substr(pos);
}

std::string resolve_skyview_href(const std::string& href_value) {
    const std::string host_root = "https://skyview.gsfc.nasa.gov";
    std::string href = trim_left(href_value);
    if (href.rfind("http://", 0) == 0 || href.rfind("https://", 0) == 0) {
        return href;
    }
    if (href.rfind("//", 0) == 0) {
        return "https:" + href;
    }

    if (!href.empty() && href[0] == '/') {
        // Absolute path on host.
        return host_root + href;
    }

    // Resolve against https://skyview.gsfc.nasa.gov/current/cgi/
    const std::vector<std::string> base_segments = {"current", "cgi"};
    std::vector<std::string> resolved_segments = base_segments;

    std::stringstream ss(href);
    std::string segment;
    while (std::getline(ss, segment, '/')) {
        if (segment.empty() || segment == ".") {
            continue;
        }
        if (segment == "..") {
            if (!resolved_segments.empty()) {
                resolved_segments.pop_back();
            }
            continue;
        }
        resolved_segments.push_back(segment);
    }

    std::string resolved = host_root;
    for (const auto& part : resolved_segments) {
        resolved.push_back('/');
        resolved += part;
    }
    return resolved;
}

} // namespace

bool SkyMapFetcher::extract_quicklook_jpeg_url(const std::string& html, std::string& jpg_url) {
    // Look for a tempspace JPEG link.
    // Example: https://skyview.gsfc.nasa.gov/tempspace/fits/skvXXXXXXXXXXX.jpg
    std::regex re(R"(https://skyview\.gsfc\.nasa\.gov/tempspace/[^\s\"']+\.jpg)");
    std::smatch m;
    if (std::regex_search(html, m, re)) {
        jpg_url = m.str(0);
        return true;
    }
    // Fallback: quick look jpeg link text, allowing optional quotes and relative paths.
    std::regex re2(R"(href\s*=\s*['\"]?([^'\">\s]+\.jpg))", std::regex::icase);
    if (std::regex_search(html, m, re2) && m.size() >= 2) {
        std::string candidate = m.str(1);
        if (candidate.rfind("http", 0) != 0 && candidate.rfind("//", 0) != 0) {
            jpg_url = resolve_skyview_href(candidate);
        } else {
            jpg_url = resolve_skyview_href(candidate);
        }
        return true;
    }
    return false;
}

bool SkyMapFetcher::jpeg_memory_to_png_file(const std::string& jpg_bytes,
                                            const std::string& png_path,
                                            int png_stride_align) {
    int w = 0, h = 0, comp = 0;
    const stbi_uc* data = reinterpret_cast<const stbi_uc*>(jpg_bytes.data());
    stbi_uc* rgba = stbi_load_from_memory(data, static_cast<int>(jpg_bytes.size()), &w, &h, &comp, 4);
    if (!rgba) return false;

    const size_t total_bytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
    std::vector<unsigned char> pixels(rgba, rgba + total_bytes);
    stbi_image_free(rgba);

    overlay_random_texture(pixels, w, h);
    apply_sky_map_post_fx(pixels, w, h);

    const int stride_in_bytes = w * 4;
    int stride = stride_in_bytes;
    if (png_stride_align > 0) {
        const int remainder = stride_in_bytes % png_stride_align;
        if (remainder != 0) {
            stride = stride_in_bytes + (png_stride_align - remainder);
        }
    }

    return stbi_write_png(png_path.c_str(), w, h, 4, pixels.data(), stride) != 0;
}

SkyMapResult SkyMapFetcher::fetch_and_save_png(const std::string& output_png_path,
                                               int pixels,
                                               double fov_deg,
                                               const std::string& survey) {
    SkyMapResult res;

    // 1) Geolocate
    double lat = 0.0, lon = 0.0; std::string city;
    if (!geolocate_ip(lat, lon, city)) {
        res.ok = false;
        res.message = "Failed to geolocate IP";
        return res;
    }
    res.latitude_deg = lat;
    res.longitude_deg = lon;

    // 2) Zenith RA/Dec now
    auto radec = zenith_radec(lat, lon, std::time(nullptr));
    res.ra_deg = radec.first;
    res.dec_deg = radec.second;

    // 3) Build SkyView request
    std::string query_url = build_skyview_url(res.ra_deg, res.dec_deg, pixels, fov_deg, survey);

    // 4) Get SkyView results page
    std::string html; long code = 0;
    if (!http_get_text(query_url, html, code)) {
        res.ok = false;
        res.message = "SkyView query failed. HTTP " + std::to_string(code);
        return res;
    }

    // 5) Find the quicklook JPEG
    std::string jpg_url;
    if (!extract_quicklook_jpeg_url(html, jpg_url)) {
        res.ok = false;
        res.message = "Could not find quicklook JPEG in SkyView response";
        return res;
    }
    res.fetched_jpeg_url = jpg_url;

    // 6) Download JPEG
    std::string jpg_bytes; long img_code = 0;
    if (!http_get_binary(jpg_url, jpg_bytes, img_code)) {
        res.ok = false;
        res.message = "Failed to download JPEG. HTTP " + std::to_string(img_code);
        return res;
    }

    // 7) Convert to PNG
    if (!jpeg_memory_to_png_file(jpg_bytes, output_png_path)) {
        res.ok = false;
        res.message = "JPEG to PNG transcode failed";
        return res;
    }

    res.saved_png_path = output_png_path;
    res.ok = true;
    res.message = "OK";
    return res;
}

SkyMapResult SkyMapFetcher::fetch_or_load_cached(const std::filesystem::path& output_png_path,
                                                 const std::filesystem::path& metadata_path,
                                                 std::chrono::seconds max_age,
                                                 int pixels,
                                                 double fov_deg,
                                                 const std::string& survey) {
    namespace fs = std::filesystem;

    const auto now = std::chrono::system_clock::now();
    const std::time_t now_utc = std::chrono::system_clock::to_time_t(now);

    // Attempt to reuse cached image if metadata indicates it is recent enough.
    if (fs::exists(output_png_path) && fs::exists(metadata_path)) {
        try {
            std::ifstream meta_in(metadata_path);
            if (meta_in) {
                json meta_json;
                meta_in >> meta_json;
                const std::time_t created_utc = static_cast<std::time_t>(meta_json.value("created_utc", static_cast<long long>(0)));
                if (created_utc > 0) {
                    const auto age = now_utc - created_utc;
                    if (age >= 0 && std::chrono::seconds(age) <= max_age) {
                        bool allow_reuse = true;
                        const double cached_contrast = meta_json.value("contrast_factor", 0.0);
                        if (std::abs(cached_contrast - static_cast<double>(kSkyContrastFactor)) > 1e-3) {
                            allow_reuse = false;
                        }
                        const double cached_hash = meta_json.value("hash_strength", -1.0);
                        if (std::abs(cached_hash - static_cast<double>(kSkyHashStrength)) > 1e-3) {
                            allow_reuse = false;
                        }
                        const double cached_smoothing = meta_json.value("line_smoothing_strength", -1.0);
                        if (std::abs(cached_smoothing - static_cast<double>(kSkyLineSmoothStrength)) > 1e-3) {
                            allow_reuse = false;
                        }
                        if (allow_reuse) {
                            SkyMapResult res;
                            res.ok = true;
                            res.reused_cached = true;
                            res.message = "Reused cached sky map";
                            res.latitude_deg = meta_json.value("latitude_deg", 0.0);
                            res.longitude_deg = meta_json.value("longitude_deg", 0.0);
                            res.ra_deg = meta_json.value("ra_deg", 0.0);
                            res.dec_deg = meta_json.value("dec_deg", 0.0);
                            res.fetched_jpeg_url = meta_json.value("fetched_jpeg_url", std::string());
                            res.saved_png_path = fs::absolute(output_png_path).u8string();
                            res.created_at_utc = created_utc;
                            return res;
                        }
                    }
                }
            }
        } catch (const std::exception&) {
            // Fall back to fetching a new image if metadata cannot be read.
        }
    }

    fs::path output_abs = fs::absolute(output_png_path);
    try {
        fs::path output_parent = output_abs.parent_path();
        if (!output_parent.empty()) {
            fs::create_directories(output_parent);
        }
    } catch (const std::exception&) {
        // Ignore directory creation failures and let the fetch call report errors.
    }

    SkyMapResult res = fetch_and_save_png(output_abs.u8string(), pixels, fov_deg, survey);
    if (!res.ok) {
        return res;
    }

    res.reused_cached = false;
    res.created_at_utc = now_utc;
    res.saved_png_path = output_abs.u8string();

    json meta_json;
    meta_json["created_utc"] = static_cast<long long>(now_utc);

    std::tm utc_tm{};
#if defined(_WIN32)
    gmtime_s(&utc_tm, &now_utc);
#else
    gmtime_r(&now_utc, &utc_tm);
#endif
    std::ostringstream iso8601;
    iso8601 << std::put_time(&utc_tm, "%Y-%m-%dT%H:%M:%SZ");
    meta_json["created_iso8601"] = iso8601.str();

    meta_json["latitude_deg"] = res.latitude_deg;
    meta_json["longitude_deg"] = res.longitude_deg;
    meta_json["ra_deg"] = res.ra_deg;
    meta_json["dec_deg"] = res.dec_deg;
    meta_json["png_path"] = output_abs.u8string();
    meta_json["fetched_jpeg_url"] = res.fetched_jpeg_url;
    meta_json["survey"] = survey;
    meta_json["pixels"] = pixels;
    meta_json["fov_deg"] = fov_deg;
    meta_json["post_processed_iso8601"] = iso8601.str();
    meta_json["contrast_factor"] = kSkyContrastFactor;
    meta_json["hash_strength"] = kSkyHashStrength;
    meta_json["line_smoothing_strength"] = kSkyLineSmoothStrength;

    try {
        fs::path meta_parent = metadata_path.parent_path();
        if (!meta_parent.empty()) {
            fs::create_directories(meta_parent);
        }
        std::ofstream meta_out(metadata_path, std::ios::trunc);
        if (meta_out) {
            meta_out << meta_json.dump(2);
        }
    } catch (const std::exception&) {
        // Ignore metadata persistence errors; fetching succeeded.
    }

    return res;
}
