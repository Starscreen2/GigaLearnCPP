#pragma once

#include "Report.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <chrono>
#include <nlohmann/json.hpp>

void GGL::Report::Display(std::vector<std::string> keyRows) const {
	std::stringstream stream;
	stream << std::string(8, '\n');
	stream << RG_DIVIDER << std::endl;
	for (std::string row : keyRows) {
		if (!row.empty()) {

			int indentLevel = 0;
			while (row[0] == '-') {
				indentLevel++;
				row.erase(row.begin());
			}

			std::string prefix = {};
			if (indentLevel > 0) {
				prefix += std::string((indentLevel - 1) * 3, ' ');
				prefix += " - ";
			}
			if (Has(row)) {
				stream << prefix << SingleToString(row, true) << std::endl;
			} else {
				continue;
			}
		} else {
			stream << std::endl;
		}
	}

	stream << std::string(4, '\n');

	std::cout << stream.str();
}

void GGL::Report::DisplayBriefSummary() const {
	std::stringstream stream;
	stream << std::string(2, '\n');
	stream << "========================================" << std::endl;
	stream << "=== TRAINING SUMMARY ===" << std::endl;
	stream << "========================================" << std::endl;
	
	// Key training stats
	std::vector<std::string> keyMetrics = {
		"Total Timesteps",
		"Total Iterations",
		"Average Step Reward",
		"Overall Steps/Second"
	};
	
	for (const std::string& key : keyMetrics) {
		if (Has(key)) {
			stream << SingleToString(key, true) << std::endl;
		}
	}
	
	stream << "========================================" << std::endl;
	stream << std::endl;
	
	std::cout << stream.str();
}

void GGL::Report::ExportFullSummary(const std::filesystem::path& basePath, 
                                     const std::vector<std::unordered_map<std::string, double>>& metricHistory) const {
	using namespace nlohmann;
	
	// Create JSON object
	json j = json::object();
	
	// Organize metrics by category
	json trainingStats = json::object();
	json rewardMetrics = json::object();
	json playerMetrics = json::object();
	json performanceMetrics = json::object();
	json gameMetrics = json::object();
	json otherMetrics = json::object();
	
	// Categorize all metrics
	for (const auto& pair : data) {
		const std::string& key = pair.first;
		const Val& value = pair.second;
		
		if (key.find("Rewards/") == 0) {
			// Remove "Rewards/" prefix for cleaner JSON
			std::string cleanKey = key.substr(8);
			rewardMetrics[cleanKey] = value;
		} else if (key.find("Player/") == 0) {
			std::string cleanKey = key.substr(7);
			playerMetrics[cleanKey] = value;
		} else if (key.find("Game/") == 0) {
			std::string cleanKey = key.substr(5);
			gameMetrics[cleanKey] = value;
		} else if (key == "Total Timesteps" || key == "Total Iterations" || 
		           key == "Average Step Reward" || key == "Policy Entropy" ||
		           key == "KL Div Loss" || key == "First Accuracy") {
			trainingStats[key] = value;
		} else if (key.find("Steps/Second") != std::string::npos || 
		           key.find("Time") != std::string::npos) {
			performanceMetrics[key] = value;
		} else {
			otherMetrics[key] = value;
		}
	}
	
	// Add timestamp
	auto now = std::chrono::system_clock::now();
	auto time_t = std::chrono::system_clock::to_time_t(now);
	std::stringstream timeStr;
	timeStr << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
	j["timestamp"] = timeStr.str();
	
	// Add categories
	if (!trainingStats.empty()) j["training_stats"] = trainingStats;
	if (!rewardMetrics.empty()) j["reward_metrics"] = rewardMetrics;
	if (!playerMetrics.empty()) j["player_metrics"] = playerMetrics;
	if (!performanceMetrics.empty()) j["performance_metrics"] = performanceMetrics;
	if (!gameMetrics.empty()) j["game_metrics"] = gameMetrics;
	if (!otherMetrics.empty()) j["other_metrics"] = otherMetrics;
	
	// Add time series data for graph plotting
	// Each snapshot includes iteration, timesteps, and key metrics
	if (!metricHistory.empty()) {
		json timeSeries = json::array();
		for (const auto& snapshot : metricHistory) {
			json snapshotJson = json::object();
			for (const auto& metric : snapshot) {
				snapshotJson[metric.first] = metric.second;
			}
			timeSeries.push_back(snapshotJson);
		}
		j["time_series"] = timeSeries;
		j["time_series_count"] = metricHistory.size();
		j["time_series_interval"] = "Every 100 iterations or at checkpoints";
	}
	
	// Write JSON file
	std::filesystem::path jsonPath = basePath;
	jsonPath.replace_extension(".json");
	std::ofstream jsonFile(jsonPath);
	if (jsonFile.good()) {
		jsonFile << j.dump(4) << std::endl;
		jsonFile.close();
	}
	
	// Write human-readable text file
	std::filesystem::path txtPath = basePath;
	txtPath.replace_extension(".txt");
	std::ofstream txtFile(txtPath);
	if (txtFile.good()) {
		txtFile << "========================================" << std::endl;
		txtFile << "=== FULL TRAINING SUMMARY ===" << std::endl;
		txtFile << "========================================" << std::endl;
		txtFile << "Timestamp: " << timeStr.str() << std::endl;
		txtFile << std::endl;
		
		// Training Stats
		if (!trainingStats.empty()) {
			txtFile << "--- Training Stats ---" << std::endl;
			for (const auto& item : trainingStats.items()) {
				txtFile << item.key() << ": " << Utils::NumToStr(item.value().get<double>()) << std::endl;
			}
			txtFile << std::endl;
		}
		
		// Reward Metrics
		if (!rewardMetrics.empty()) {
			txtFile << "--- Reward Metrics ---" << std::endl;
			for (const auto& item : rewardMetrics.items()) {
				txtFile << item.key() << ": " << Utils::NumToStr(item.value().get<double>()) << std::endl;
			}
			txtFile << std::endl;
		}
		
		// Player Metrics
		if (!playerMetrics.empty()) {
			txtFile << "--- Player Metrics ---" << std::endl;
			for (const auto& item : playerMetrics.items()) {
				txtFile << item.key() << ": " << Utils::NumToStr(item.value().get<double>()) << std::endl;
			}
			txtFile << std::endl;
		}
		
		// Performance Metrics
		if (!performanceMetrics.empty()) {
			txtFile << "--- Performance Metrics ---" << std::endl;
			for (const auto& item : performanceMetrics.items()) {
				txtFile << item.key() << ": " << Utils::NumToStr(item.value().get<double>()) << std::endl;
			}
			txtFile << std::endl;
		}
		
		// Game Metrics
		if (!gameMetrics.empty()) {
			txtFile << "--- Game Metrics ---" << std::endl;
			for (const auto& item : gameMetrics.items()) {
				txtFile << item.key() << ": " << Utils::NumToStr(item.value().get<double>()) << std::endl;
			}
			txtFile << std::endl;
		}
		
		// Other Metrics
		if (!otherMetrics.empty()) {
			txtFile << "--- Other Metrics ---" << std::endl;
			for (const auto& item : otherMetrics.items()) {
				txtFile << item.key() << ": " << Utils::NumToStr(item.value().get<double>()) << std::endl;
			}
			txtFile << std::endl;
		}
		
		txtFile << "========================================" << std::endl;
		txtFile.close();
	}
}
