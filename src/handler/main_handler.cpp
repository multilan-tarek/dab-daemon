#include "main_handler.h"
#include <algorithm>
#include <csignal>
#include <format>
#include <vector>
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
        sdbus::registerProperty("Version").withGetter([]() {
            return std::string{"1.0"};
        }).withUpdateBehavior(sdbus::Flags::CONST_PROPERTY_VALUE),
        sdbus::registerProperty("Tuners").withGetter([this]() {
            return get_tuner_paths();
        })
    ).forInterface("de.multilan.dabdaemon.Manager1");

    dbus_connection->addObjectManager(sdbus::ObjectPath{"/de/multilan/dabdaemon"});
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

void MainHandler::handle() {
    usb_handler->handle();
    retry_pending_devices();

    std::vector<Device *> current_devices;

    {
        const std::lock_guard lock(devices_mutex);
        current_devices = devices;
    }

    for (const auto device: current_devices) {
        device->dbus_handler->emit_pending_property_changes();
    }
}

void MainHandler::device_connected_event(const USBHandler::Device usb_device) {
    {
        const std::lock_guard lock(devices_mutex);

        for (const auto device: devices) {
            if (device->usb_device == usb_device) {
                logger.warning(std::format("USB device on bus {}, address {} is already registered", usb_device.bus, usb_device.address));
                return;
            }
        }
    }

    const auto next_attempt = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    const auto pending = std::ranges::find(pending_devices, usb_device, &PendingDevice::usb_device);

    if (pending == pending_devices.end()) {
        pending_devices.push_back({usb_device, next_attempt});
    } else {
        pending->next_attempt = next_attempt;
    }

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

    if (!device->tuner_handler->is_ready()) {
        logger.error(std::format("Tuner {} hardware initialization failed", device->dbus_id));
        delete device->tuner_handler;
        delete device;
        return;
    }

    device->dbus_handler = new DBusHandler(device->dbus_id, dbus_connection, device->tuner_handler);

    {
        const std::lock_guard lock(devices_mutex);
        devices.push_back(device);
    }

    std::erase_if(pending_devices, [&usb_device](const PendingDevice &pending_device) {
        return pending_device.usb_device == usb_device;
    });

    try {
        device->dbus_handler->emit_interfaces_added_signal();
        dbus_root_object->emitPropertiesChangedSignal("de.multilan.dabdaemon.Manager1", std::vector<sdbus::PropertyName>{sdbus::PropertyName{"Tuners"}});
    } catch (const sdbus::Error &error) {
        logger.warning(std::format("Could not announce tuner {} on D-Bus: {}", device->dbus_id, error.getMessage()));
    }

    logger.info(std::format("Tuner {} registered", device->dbus_id));
}

void MainHandler::device_disconnected_event(const USBHandler::Device usb_device) {
    const auto pending_count = std::erase_if(pending_devices, [&usb_device](const PendingDevice &pending_device) {
        return pending_device.usb_device == usb_device;
    });
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
        if (pending_count == 0) {
            logger.debug(std::format("Disconnected USB device on bus {}, address {} was not registered", usb_device.bus, usb_device.address));
        }
        return;
    }

    const int id = removed_device->dbus_id;
    try {
        removed_device->dbus_handler->emit_interfaces_removed_signal();
    } catch (const sdbus::Error &error) {
        logger.warning(std::format("Could not announce tuner {} removal on D-Bus: {}", id, error.getMessage()));
    }

    delete removed_device->dbus_handler;
    delete removed_device->tuner_handler;
    delete removed_device;

    try {
        dbus_root_object->emitPropertiesChangedSignal("de.multilan.dabdaemon.Manager1", std::vector<sdbus::PropertyName>{sdbus::PropertyName{"Tuners"}});
    } catch (const sdbus::Error &error) {
        logger.warning(std::format("Could not update tuner list on D-Bus: {}", error.getMessage()));
    }

    logger.info(std::format("Tuner {} removed", id));
}

void MainHandler::retry_pending_devices() {
    const auto now = std::chrono::steady_clock::now();
    std::vector<USBHandler::Device> retry_devices;

    for (const auto &pending_device: pending_devices) {
        if (now >= pending_device.next_attempt) {
            retry_devices.push_back(pending_device.usb_device);
        }
    }

    for (const auto &usb_device: retry_devices) {
        logger.debug(std::format("Retrying USB device on bus {}, address {}", usb_device.bus, usb_device.address));
        device_connected_event(usb_device);
    }
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
