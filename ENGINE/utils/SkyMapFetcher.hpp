#pragma once
#include <chrono>
#include <ctime>
#include <filesystem>
#include <string>

struct SkyMapResult {
    bool ok = false;
    std::string message;
    bool reused_cached = false;
    double latitude_deg = 0.0;
    double longitude_deg = 0.0;
    double ra_deg = 0.0;
    double dec_deg = 0.0;
    std::time_t created_at_utc = 0;
    std::string saved_png_path;
    std::string fetched_jpeg_url;
};

class SkyMapFetcher {
public:

    SkyMapResult fetch_and_save_png(const std::string& output_png_path, int pixels = 3000, double fov_deg = 8.0, const std::string& survey = "DSS2 Red");

    SkyMapResult fetch_or_load_cached(const std::filesystem::path& output_png_path,
                                      const std::filesystem::path& metadata_path,
                                      std::chrono::seconds max_age = std::chrono::seconds{3600},
                                      int pixels = 3000,
                                      double fov_deg = 8.0,
                                      const std::string& survey = "DSS2 Red");

    static std::pair<double,double> zenith_radec(double latitude_deg, double longitude_deg, std::time_t utc_seconds_since_epoch = 0);

private:
    static bool http_get_text(const std::string& url, std::string& out_text, long& http_code);
    static bool http_get_binary(const std::string& url, std::string& out_bytes, long& http_code);

    static bool geolocate_ip(double& lat_deg, double& lon_deg, std::string& city);
    static double julian_date(std::time_t t);
    static double gmst_deg(double jd);
    static std::string build_skyview_url(double ra_deg, double dec_deg, int pixels, double fov_deg, const std::string& survey);
    static bool extract_quicklook_jpeg_url(const std::string& html, std::string& jpg_url);

    static bool jpeg_memory_to_png_file(const std::string& jpg_bytes, const std::string& png_path, int png_stride_align = 0 );
};
