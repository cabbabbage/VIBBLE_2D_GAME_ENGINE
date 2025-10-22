// SkyMapFetcher.cpp
#include "SkyMapFetcher.hpp"

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
#include <cctype>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

// stb_image and stb_image_write (public domain / MIT-like).
// Embedded here so you do not need extra libs for PNG output.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#include <stdint.h>
extern "C" {
#include "stb_image.h"
}
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

using json = nlohmann::json;

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

    int stride_in_bytes = w * 4;
    bool ok = stbi_write_png(png_path.c_str(), w, h, 4, rgba,
                             png_stride_align > 0 ? png_stride_align : stride_in_bytes) != 0;
    stbi_image_free(rgba);
    return ok;
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
