#ifndef DBUS_HANDLER_H
#define DBUS_HANDLER_H

#include <sdbus-c++/sdbus-c++.h>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include "tuner_handler.h"
#include "../helper/logger.h"

class DBusHandler {
public:
    explicit DBusHandler(int id, const std::unique_ptr<sdbus::IConnection> &connection, TunerHandler *tuner_handler);

    ~DBusHandler();

    void emit_interfaces_added_signal();
    void emit_interfaces_removed_signal();

    void emit_pending_property_changes();

private:
    struct Command {
        sdbus::Result<bool> result;
        std::function<bool()> action;
        std::chrono::steady_clock::time_point deadline;
    };

    std::unique_ptr<sdbus::IObject> object = nullptr;
    TunerHandler *tuner_handler = nullptr;
    std::mutex pending_properties_mutex;
    std::set<std::string> pending_properties;
    std::mutex command_mutex;
    std::condition_variable command_condition;
    std::deque<Command> commands;
    std::thread command_thread;
    bool commands_running = true;
    uint64_t ensembles_revision = 0;
    Logger logger{"DBUS"};

    void property_change_event(const std::vector<std::string> &properties);

    void queue_command(sdbus::Result<bool> result, const std::function<bool()> &action);

    void process_commands();
};

#endif //DBUS_HANDLER_H
