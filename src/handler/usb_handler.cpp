//
// Created by tarek on 9/30/26.
//

#include "usb_handler.h"
#include <format>
#include <iostream>

USBHandler::USBHandler(const std::function<void(Device)> &connect_callback, const std::function<void(Device)> &disconnect_callback) {
    this->connect_callback = connect_callback;
    this->disconnect_callback = disconnect_callback;

    libusb_init(&context);
}

void USBHandler::init() {
    if (libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG)) {
        libusb_hotplug_register_callback(
            context,
            LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED | LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT,
            LIBUSB_HOTPLUG_ENUMERATE,
            0x16c0,
            0x05dc,
            LIBUSB_CLASS_VENDOR_SPEC,
            hotplug_callback,
            this,
            &callback_handle
        );
    } else {
        std::cerr << "USB Handler: This libusb version does not support hotplugging" << std::endl;
    }
}

USBHandler::~USBHandler() {
    if (!context) return;

    libusb_hotplug_deregister_callback(context, callback_handle);
    libusb_exit(context);
}

void USBHandler::handle() const {
    libusb_handle_events(context);
}

int USBHandler::hotplug_callback(libusb_context *, libusb_device *device, const libusb_hotplug_event event, void *obj) {
    const auto object = static_cast<USBHandler *>(obj);

    libusb_device_descriptor desc{};

    if (libusb_get_device_descriptor(device, &desc) != 0) {
        return 0;
    }

    const uint8_t address = libusb_get_device_address(device);
    const uint8_t bus = libusb_get_bus_number(device);

    if (bus == 0 || address == 0) {
        return 0;
    }

    const auto clean_device = Device({
        desc.idVendor,
        desc.idProduct,
        bus,
        address
    });

    const std::string details_str = std::format("(Bus {}, Address {}, ID {:04X}:{:04X})", clean_device.bus, clean_device.address, clean_device.vendor_id, clean_device.device_id);

    if (event == LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED) {
        std::cout << "USB Handler: Found device " << details_str << std::endl;

        if (object->connect_callback) {
            object->connect_callback(clean_device);
        }
    } else if (event == LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT) {
        std::cout << "USB Handler: Device disconnected " << details_str << std::endl;

        if (object->disconnect_callback) {
            object->disconnect_callback(clean_device);
        }
    }

    return 0;
}

libusb_device_handle *USBHandler::get_device_handle(const Device device) const {
    libusb_device **list;
    const ssize_t count = libusb_get_device_list(context, &list);

    libusb_device_handle *handle = nullptr;

    for (ssize_t i = 0; i < count; i++) {
        libusb_device *dev = list[i];

        if (libusb_get_bus_number(dev) != device.bus || libusb_get_device_address(dev) != device.address) {
            continue;
        }

        if (libusb_open(dev, &handle) == 0) {
            break;
        }

        handle = nullptr;
    }

    libusb_free_device_list(list, 1);

    return handle;
}
