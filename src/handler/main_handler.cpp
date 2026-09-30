//
// Created by tarek on 9/30/26.
//

#include "main_handler.h"
#include <iostream>
#include "../helper/partial.h"

MainHandler::MainHandler() {
    dbus_connection = sdbus::createSessionBusConnection(
        sdbus::ServiceName("de.multilan.dabdaemon")
    );

    dbus_root_object = sdbus::createObject(
        *dbus_connection,
        sdbus::ObjectPath{"/de/multilan/dabdaemon"}
    );

    dbus_root_object->addVTable(
        sdbus::registerProperty("Version")
        .withGetter([]() {
            return std::string{"1.0"};
        })
    ).forInterface("de.multilan.dabdaemon.Manager1");

    dbus_connection->enterEventLoopAsync();

    usb_handler = new USBHandler(
        partial(&MainHandler::device_connected_event, this),
        partial(&MainHandler::device_disconnected_event, this)
    );

    usb_handler->init();
}

MainHandler::~MainHandler() {
    delete usb_handler;

    for (const auto device: devices) {
        delete device->dbus_handler;
        delete device->tuner_handler;
        delete device;
    }
}

void MainHandler::handle() const {
    usb_handler->handle();
}

void MainHandler::device_connected_event(const USBHandler::Device usb_device) {
    const auto device = new Device();
    const auto device_handle = usb_handler->get_device_handle(usb_device);

    if (!device_handle) {
        std::cerr << "Main Handler: Could not open device" << std::endl;
    }

    device->usb_device = usb_device;
    device->tuner_handler = new TunerHandler(device_handle);
    device->dbus_id = generate_tuner_id();
    device->dbus_handler = new DBusHandler(device->dbus_id, dbus_connection, device->tuner_handler);

    devices.push_back(device);
}

void MainHandler::device_disconnected_event(const USBHandler::Device usb_device) {
    for (auto device: devices) {
        if (device->usb_device == usb_device) {
            std::erase(devices, device);
        }
    }
}

int MainHandler::generate_tuner_id() const {
    int id = 0;

    for (const auto device: devices) {
        if (device->dbus_id == id) {
            id++;
        }
    }

    return id;
}
