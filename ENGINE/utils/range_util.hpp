#pragma once

#include <vector>
#include <SDL.h>

class Asset;

/*
  radial and distance helpers
  supports asset and point overloads
*/

class Range {
 public:
  
  static bool is_in_range(const Asset* a, const Asset* b, int radius);
  static bool is_in_range(const Asset* a, const SDL_Point& b, int radius);
  static bool is_in_range(const Asset* a, const SDL_FPoint& b, int radius);
  static bool is_in_range(const SDL_Point& a, const Asset* b, int radius);
  static bool is_in_range(const SDL_FPoint& a, const Asset* b, int radius);
  static bool is_in_range(const SDL_Point& a, const SDL_Point& b, int radius);
  static bool is_in_range(const SDL_Point& a, const SDL_FPoint& b, int radius);
  static bool is_in_range(const SDL_FPoint& a, const SDL_Point& b, int radius);
  static bool is_in_range(const SDL_FPoint& a, const SDL_FPoint& b, int radius);

  
  static double get_distance(const Asset* a, const Asset* b);
  static double get_distance(const Asset* a, const SDL_Point& b);
  static double get_distance(const Asset* a, const SDL_FPoint& b);
  static double get_distance(const SDL_Point& a, const Asset* b);
  static double get_distance(const SDL_FPoint& a, const Asset* b);
  static double get_distance(const SDL_Point& a, const SDL_Point& b);
  static double get_distance(const SDL_Point& a, const SDL_FPoint& b);
  static double get_distance(const SDL_FPoint& a, const SDL_Point& b);
  static double get_distance(const SDL_FPoint& a, const SDL_FPoint& b);

  
  static std::vector<Asset*> get_in_range(const Asset* center,
                                          int radius,
                                          const std::vector<Asset*>& candidates);

  static std::vector<Asset*> get_in_range(const SDL_Point& center,
                                          int radius,
                                          const std::vector<Asset*>& candidates);

  static std::vector<Asset*> get_in_range(const SDL_FPoint& center,
                                          int radius,
                                          const std::vector<Asset*>& candidates);

  
  static void get_in_range(const Asset* center,
                           int radius,
                           const std::vector<Asset*>& candidates,
                           std::vector<Asset*>& out);

  static void get_in_range(const SDL_Point& center,
                           int radius,
                           const std::vector<Asset*>& candidates,
                           std::vector<Asset*>& out);

  static void get_in_range(const SDL_FPoint& center,
                           int radius,
                           const std::vector<Asset*>& candidates,
                           std::vector<Asset*>& out);

 private:
  
  static bool xy(const Asset* a, double& x, double& y);
  static bool xy(const SDL_Point& p, double& x, double& y);
  static bool xy(const SDL_FPoint& p, double& x, double& y);

  static bool in_range_xy(double ax, double ay, double bx, double by, int radius);
  static double distance_xy(double ax, double ay, double bx, double by);
};
