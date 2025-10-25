#pragma once

#include <utility>

class RoomRelativeSizeResolver {
public:
    RoomRelativeSizeResolver(int original_width, int original_height, int current_width, int current_height);

    int original_width() const { return original_width_; }
    int original_height() const { return original_height_; }
    int current_width() const { return current_width_; }
    int current_height() const { return current_height_; }

    double width_ratio() const { return width_ratio_; }
    double height_ratio() const { return height_ratio_; }
    double average_ratio() const { return average_ratio_; }

    int scale_count(int value) const;
    std::pair<int, int> scale_count_range(int min_value, int max_value) const;
    int scale_length(int value) const;

private:
    static int sanitize_dimension(int value, int fallback);
    static double safe_ratio(int numerator, int denominator);

    int original_width_;
    int original_height_;
    int current_width_;
    int current_height_;
    double width_ratio_;
    double height_ratio_;
    double average_ratio_;
};

