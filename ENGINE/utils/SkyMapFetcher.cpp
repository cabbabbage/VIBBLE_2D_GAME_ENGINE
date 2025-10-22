// SkyMapFetcher.cpp
#include "SkyMapFetcher.hpp"

#include <ctime>
#include <cmath>
#include <regex>
#include <vector>
#include <stdexcept>
#include <iostream>

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

bool SkyMapFetcher::extract_quicklook_jpeg_url(const std::string& html, std::string& jpg_url) {
    // Look for a tempspace JPEG link.
    // Example: https://skyview.gsfc.nasa.gov/tempspace/fits/skvXXXXXXXXXXX.jpg
    std::regex re(R"(https://skyview\.gsfc\.nasa\.gov/tempspace/[^\s\"']+\.jpg)");
    std::smatch m;
    if (std::regex_search(html, m, re)) {
        jpg_url = m.str(0);
        return true;
    }
    // Fallback: quick look jpeg link text
    std::regex re2(R"(href=['\"]([^'\"]+\.jpg)['\"])");
    if (std::regex_search(html, m, re2)) {
        jpg_url = m.str(1);
        if (jpg_url.rfind("http", 0) != 0) {
            // relative path
            jpg_url = "https://skyview.gsfc.nasa.gov" + jpg_url;
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
