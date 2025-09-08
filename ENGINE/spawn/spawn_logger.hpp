#pragma once

#include <string>
#include <chrono>
#include <memory>
#include "asset\asset_info.hpp"

class SpawnLogger {

	public:
    SpawnLogger(const std::string& map_dir, std::string room_dir);
    void start_timer();
    void output_and_log(const std::string& asset_name, int quantity, int spawned, int attempts, int max_attempts, const std::string& method);
    void progress(const std::shared_ptr<AssetInfo>& info, int current, int total);

	private:
    std::string map_dir_;
    std::string room_dir_;
    std::chrono::time_point<std::chrono::steady_clock> start_time_;
};
