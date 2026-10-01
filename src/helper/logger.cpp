//
// Created by tarek on 10/1/26.
//

#include "logger.h"
#include <chrono>
#include <ctime>
#include <format>
#include <iostream>

std::atomic<LogLevel> Logger::minimum_level = LogLevel::INFO;
std::mutex Logger::output_mutex;

Logger::Logger(const std::string &component) {
    this->component = component;
}

void Logger::set_id(const int id) {
    this->id = id;
}

void Logger::debug(const std::string &message) const {
    log(LogLevel::DEBUG, message);
}

void Logger::info(const std::string &message) const {
    log(LogLevel::INFO, message);
}

void Logger::warning(const std::string &message) const {
    log(LogLevel::WARNING, message);
}

void Logger::error(const std::string &message) const {
    log(LogLevel::ERROR, message);
}

void Logger::set_level(const LogLevel level) {
    minimum_level = level;
}

void Logger::log(const LogLevel level, const std::string &message) const {
    if (static_cast<uint8_t>(level) < static_cast<uint8_t>(minimum_level.load())) {
        return;
    }

    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;

    std::tm local_time{};
    localtime_r(&time, &local_time);

    char time_buffer[9]{};
    std::strftime(time_buffer, sizeof(time_buffer), "%H:%M:%S", &local_time);

    const std::lock_guard lock(output_mutex);

    if (id >= 0) {
        std::cerr << std::format("[{}.{:03}] [{:<7}] [{} {}] {}\n", time_buffer, milliseconds, get_level_name(level), component, id, message);
    } else {
        std::cerr << std::format("[{}.{:03}] [{:<7}] [{}] {}\n", time_buffer, milliseconds, get_level_name(level), component, message);
    }
}

const char *Logger::get_level_name(const LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG:
            return "DEBUG";
        case LogLevel::INFO:
            return "INFO";
        case LogLevel::WARNING:
            return "WARNING";
        case LogLevel::ERROR:
            return "ERROR";
    }

    return "UNKNOWN";
}
