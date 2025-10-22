#pragma once
#include <string>

struct SkyMapResult {
    bool ok = false;
    std::string message;
    double latitude_deg = 0.0;
    double longitude_deg = 0.0;
    double ra_deg = 0.0;   // center RA used
    double dec_deg = 0.0;  // center Dec used
    std::string saved_png_path;
    std::string fetched_jpeg_url;
};

class SkyMapFetcher {
public:
    // Fetch a star-field PNG of the sky above the current IP location "now".
    // output_png_path: where to save the PNG.
    // pixels: output image width=height in pixels.
    // fov_deg: square field of view in degrees.
    // survey: SkyView survey name (e.g., "DSS2 Red", "DSS2 Blue", "DSS").
    SkyMapResult fetch_and_save_png(const std::string& output_png_path,
                                    int pixels = 3000,
                                    double fov_deg = 8.0,
                                    const std::string& survey = "DSS2 Red");

    // Utility if you want to supply your own location and time.
    // utc_seconds_since_epoch is time_t in UTC. If 0, uses now.
    static std::pair<double,double> zenith_radec(double latitude_deg,
                                                 double longitude_deg,
                                                 std::time_t utc_seconds_since_epoch = 0);

private:
    static bool http_get_text(const std::string& url, std::string& out_text, long& http_code);
    static bool http_get_binary(const std::string& url, std::string& out_bytes, long& http_code);

    static bool geolocate_ip(double& lat_deg, double& lon_deg, std::string& city);
    static double julian_date(std::time_t t);
    static double gmst_deg(double jd);
    static std::string build_skyview_url(double ra_deg, double dec_deg,
                                         int pixels, double fov_deg,
                                         const std::string& survey);
    static bool extract_quicklook_jpeg_url(const std::string& html, std::string& jpg_url);

    static bool jpeg_memory_to_png_file(const std::string& jpg_bytes,
                                        const std::string& png_path,
                                        int png_stride_align = 0 /*0 = default*/);
};
