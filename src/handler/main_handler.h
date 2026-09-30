//
// Created by tarek on 9/30/26.
//

#ifndef MAIN_HANDLER_H
#define MAIN_HANDLER_H

#include <sdbus-c++/sdbus-c++.h>
#include "dbus_handler.h"
#include "tuner_handler.h"
#include "usb_handler.h"

class MainHandler {
public:
    MainHandler();

    ~MainHandler();

    void handle() const;

    void device_connected_event(USBHandler::Device usb_device);

    void device_disconnected_event(USBHandler::Device usb_device);

private:
    typedef struct {
        int dbus_id;
        USBHandler::Device usb_device;
        DBusHandler *dbus_handler;
        TunerHandler *tuner_handler;
    } Device;

    USBHandler *usb_handler = nullptr;

    std::unique_ptr<sdbus::IConnection> dbus_connection = nullptr;
    std::unique_ptr<sdbus::IObject> dbus_root_object = nullptr;

    std::vector<Device *> devices;

    int generate_tuner_id() const;
};

#endif //MAIN_HANDLER_H
