#include "spawn_logger.hpp"
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <iostream>
#include <iomanip>
SpawnLogger::SpawnLogger(const std::string& map_dir,
                         std::string room_dir)
: map_dir_(map_dir),
room_dir_(std::move(room_dir)),
start_time_(std::chrono::steady_clock::now())
{}

void SpawnLogger::start_timer() {
	start_time_ = std::chrono::steady_clock::now();
}

void SpawnLogger::output_and_log(const std::string& asset_name,
                                 int quantity,
                                 int spawned,
                                 int attempts,
                                 int max_attempts,
                                 const std::string& method) {
	auto end_time = std::chrono::steady_clock::now();
	double duration_ms = std::chrono::duration<double, std::milli>(end_time - start_time_).count();
	const std::string csv_path = map_dir_ + "/spawn_log.csv";
	std::ifstream infile(csv_path);
	std::vector<std::string> lines;
	if (infile.is_open()) {
		std::string line;
		while (std::getline(infile, line)) {
			lines.push_back(line);
		}
		infile.close();
	}
	int room_line_index = -1;
	for (size_t i = 0; i < lines.size(); ++i) {
		if (lines[i].empty() && i + 3 < lines.size()
		&& lines[i + 1].empty() && lines[i + 2].empty()
      && lines[i + 3] == room_dir_) {
			room_line_index = static_cast<int>(i + 3);
			break;
		}
	}
	if (room_line_index == -1) {
		lines.emplace_back("");
		lines.emplace_back("");
		lines.emplace_back("");
		room_line_index = static_cast<int>(lines.size());
		lines.push_back(room_dir_);
	}
	int insert_index = room_line_index + 1;
	int asset_line_index = -1;
	while (insert_index < static_cast<int>(lines.size()) && !lines[insert_index].empty()) {
		std::istringstream ss(lines[insert_index]);
		std::string first_col;
		std::getline(ss, first_col, ',');
		if (first_col == asset_name) {
			asset_line_index = insert_index;
			break;
		}
		++insert_index;
	}
	int total_success = spawned;
	int total_attempts = attempts;
	double new_percent = total_attempts > 0 ? static_cast<double>(total_success) / total_attempts : 0.0;
	double average_time = duration_ms;
	int times_generated = 1;
	double delta_time = 0.0;
	if (asset_line_index != -1) {
		std::istringstream ss(lines[asset_line_index]);
		std::string name, percent_str, success_str, attempts_str, method_str, avg_time_str, times_gen_str;
		std::getline(ss, name, ',');
		std::getline(ss, percent_str, ',');
		std::getline(ss, success_str, ',');
		std::getline(ss, attempts_str, ',');
		std::getline(ss, method_str, ',');
		std::getline(ss, avg_time_str, ',');
		std::getline(ss, times_gen_str, ',');
		if (method_str == method) {
			total_success   += std::stoi(success_str);
			total_attempts  += std::stoi(attempts_str);
			new_percent = total_attempts > 0 ? static_cast<double>(total_success) / total_attempts : 0.0;
			double prev_avg_time    = std::stod(avg_time_str);
			int    prev_generations = std::stoi(times_gen_str);
			average_time   = (prev_avg_time * prev_generations + duration_ms) / (prev_generations + 1);
			times_generated = prev_generations + 1;
			delta_time     = duration_ms - prev_avg_time;
		} else {
			total_success   = spawned;
			total_attempts  = attempts;
			new_percent     = total_attempts > 0 ? static_cast<double>(total_success) / total_attempts : 0.0;
			average_time    = duration_ms;
			times_generated = 1;
			delta_time      = 0.0;
		}
	} else {
		asset_line_index = insert_index;
		lines.insert(lines.begin() + asset_line_index, "");
	}
	std::ostringstream updated_line;
	updated_line << asset_name << ","
	<< std::fixed << std::setprecision(3) << new_percent << ","
	<< total_success << ","
	<< total_attempts << ","
	<< method << ","
	<< std::fixed << std::setprecision(3) << average_time << ","
	<< times_generated << ","
	<< std::fixed << std::setprecision(3) << delta_time;
	lines[asset_line_index] = updated_line.str();
	std::ofstream outfile(csv_path);
	if (outfile.is_open()) {
		for (const auto& l : lines) {
			outfile << l << "\n";
		}
		outfile.close();
	}
}

void SpawnLogger::progress(const std::shared_ptr<AssetInfo>& info, int current, int total) {
	const int bar_width = 50;
	double percent = (total > 0) ? static_cast<double>(current) / total : 0.0;
	int filled = static_cast<int>(percent * bar_width);
	std::string bar(filled, '#');
	bar.resize(bar_width, '-');
	std::ostringstream oss;
	oss << "[Checking] " << std::left << std::setw(20) << info->name
	<< "[" << bar << "] "
	<< std::setw(3) << static_cast<int>(percent * 100) << "%\r";
	std::cout << oss.str() << std::flush;
}
