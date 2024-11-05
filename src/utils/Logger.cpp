#include "Logger.h"
#include <chrono>
#include <ctime>
#include <iomanip>

void Logger::logInfo(const std::string& message) {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::cout << "[INFO " << std::put_time(std::localtime(&time), "%H:%M:%S") << "] " 
              << message << std::endl;
}

void Logger::logError(const std::string& message) {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::cerr << "[ERROR " << std::put_time(std::localtime(&time), "%H:%M:%S") << "] " 
              << message << std::endl;
}

void Logger::logPerformance(const std::string& message) {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::cout << "[PERF " << std::put_time(std::localtime(&time), "%H:%M:%S") << "] " 
              << message << std::endl;
} 