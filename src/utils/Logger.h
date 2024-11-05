#pragma once
#include <iostream>
#include <string>

class Logger {
public:
    static void logInfo(const std::string& message);
    static void logError(const std::string& message);
    static void logPerformance(const std::string& message);
}; 