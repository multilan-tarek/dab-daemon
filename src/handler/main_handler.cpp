//
// Created by tarek on 9/30/26.
//

#include "main_handler.h"
#include <csignal>
#include <format>
#include "../helper/partial.h"

MainHandler::MainHandler() {
    std::signal(SIGPIPE, SIG_IGN);
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
        }),
        sdbus::registerMethod("ListTuners").withOutputParamNames("tuners").implementedAs([this]() {
            return get_tuner_paths();
        })
    ).forInterface("de.multilan.dabdaemon.Manager1");

    dbus_connection->enterEventLoopAsync();
    logger.debug("D-Bus service registered");

    usb_handler = new USBHandler(
        partial(&MainHandler::device_connected_event, this),
        partial(&MainHandler::device_disconnected_event, this)
    );

    usb_handler->init();
    logger.info("Ready");
}

MainHandler::~MainHandler() {
    std::vector<Device *> current_devices;

    {
        const std::lock_guard lock(devices_mutex);
        current_devices = devices;
        devices.clear();
    }

    for (const auto device: current_devices) {
        delete device->dbus_handler;
        delete device->tuner_handler;
        delete device;
    }

    delete usb_handler;
}

void MainHandler::handle() const {
    usb_handler->handle();
}

void MainHandler::device_connected_event(const USBHandler::Device usb_device) {
    const auto device = new Device();
    const auto device_handle = usb_handler->get_device_handle(usb_device);

    if (!device_handle) {
        logger.error(std::format("Could not open USB device on bus {}, address {}", usb_device.bus, usb_device.address));
        delete device;
        return;
    }

    device->usb_device = usb_device;
    device->dbus_id = generate_tuner_id();
    device->tuner_handler = new TunerHandler(device->dbus_id, device_handle);
    device->dbus_handler = new DBusHandler(device->dbus_id, dbus_connection, device->tuner_handler);

    {
        const std::lock_guard lock(devices_mutex);
        devices.push_back(device);
    }

    logger.info(std::format("Tuner {} registered", device->dbus_id));
}

void MainHandler::device_disconnected_event(const USBHandler::Device usb_device) {
    const Device *removed_device = nullptr;

    {
        const std::lock_guard lock(devices_mutex);

        for (const auto device: devices) {
            if (device->usb_device == usb_device) {
                removed_device = device;
                std::erase(devices, device);
                break;
            }
        }
    }

    if (!removed_device) {
        logger.warning(std::format("Disconnected USB device on bus {}, address {} was not registered", usb_device.bus, usb_device.address));
        return;
    }

    const int id = removed_device->dbus_id;
    delete removed_device->dbus_handler;
    delete removed_device->tuner_handler;
    delete removed_device;
    logger.info(std::format("Tuner {} removed", id));
}

int MainHandler::generate_tuner_id() const {
    const std::lock_guard lock(devices_mutex);
    int id = 0;

    while (true) {
        bool used = false;

        for (const auto device: devices) {
            if (device->dbus_id == id) {
                used = true;
                break;
            }
        }

        if (!used) {
            return id;
        }

        id++;
    }
}

std::vector<sdbus::ObjectPath> MainHandler::get_tuner_paths() const {
    const std::lock_guard lock(devices_mutex);
    std::vector<sdbus::ObjectPath> result;
    result.reserve(devices.size());

    for (const auto device: devices) {
        result.emplace_back(std::format("/de/multilan/dabdaemon/tuner{}", device->dbus_id));
    }

    return result;
}
