#ifndef MAIN_HANDLER_H
#define MAIN_HANDLER_H

#include <chrono>
#include <mutex>
#include <sdbus-c++/sdbus-c++.h>
#include "dbus_handler.h"
#include "tuner_handler.h"
#include "usb_handler.h"
#include "../helper/logger.h"

class MainHandler {
public:
    MainHandler();

    ~MainHandler();

    void handle();

    void device_connected_event(USBHandler::Device usb_device);

    void device_disconnected_event(USBHandler::Device usb_device);

private:
    typedef struct {
        int dbus_id;
        USBHandler::Device usb_device;
        DBusHandler *dbus_handler;
        TunerHandler *tuner_handler;
    } Device;

    struct PendingDevice {
        USBHandler::Device usb_device;
        std::chrono::steady_clock::time_point next_attempt;
    };

    USBHandler *usb_handler = nullptr;

    std::unique_ptr<sdbus::IConnection> dbus_connection = nullptr;
    std::unique_ptr<sdbus::IObject> dbus_root_object = nullptr;

    std::vector<Device *> devices;
    std::vector<PendingDevice> pending_devices;
    mutable std::mutex devices_mutex;
    Logger logger{"MAIN"};

    int generate_tuner_id() const;

    void retry_pending_devices();

    std::vector<sdbus::ObjectPath> get_tuner_paths() const;
};

#endif //MAIN_HANDLER_H
