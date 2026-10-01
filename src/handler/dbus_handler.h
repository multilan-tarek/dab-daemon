//
// Created by tarek on 9/30/26.
//

#ifndef DBUS_HANDLER_H
#define DBUS_HANDLER_H

#include <sdbus-c++/sdbus-c++.h>
#include "tuner_handler.h"
#include "../helper/logger.h"

class DBusHandler {
public:
    explicit DBusHandler(int id, const std::unique_ptr<sdbus::IConnection> &connection, TunerHandler *tuner_handler);

    ~DBusHandler();

private:
    std::unique_ptr<sdbus::IObject> object = nullptr;
    TunerHandler *tuner_handler = nullptr;
    Logger logger{"DBUS"};
};

#endif //DBUS_HANDLER_H
