#ifndef LOGGER_H
#define LOGGER_H

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

enum class LogLevel : uint8_t {
    DEBUG,
    INFO,
    WARNING,
    ERROR
};

class Logger {
public:
    explicit Logger(const std::string &component);

    void set_id(int id);

    void debug(const std::string &message) const;

    void info(const std::string &message) const;

    void warning(const std::string &message) const;

    void error(const std::string &message) const;

    static void set_level(LogLevel level);

private:
    std::string component;
    int id = -1;

    static std::atomic<LogLevel> minimum_level;
    static std::mutex output_mutex;

    void log(LogLevel level, const std::string &message) const;

    static const char *get_level_name(LogLevel level);
};

#endif //LOGGER_H
