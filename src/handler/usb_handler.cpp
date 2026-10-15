#include "usb_handler.h"
#include <format>

USBHandler::USBHandler(const std::function<void(Device)> &connect_callback, const std::function<void(Device)> &disconnect_callback) {
    this->connect_callback = connect_callback;
    this->disconnect_callback = disconnect_callback;

    const int result = libusb_init(&context);

    if (result != LIBUSB_SUCCESS) {
        logger.error(std::format("libusb initialization failed: {}", libusb_error_name(result)));
    }
}

void USBHandler::init() {
    if (!context) {
        logger.error("Cannot initialize hotplug monitoring without a libusb context");
        return;
    }

    if (!libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG)) {
        logger.error("This libusb version does not support hotplugging");
        return;
    }

    const int result = libusb_hotplug_register_callback(
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

    if (result != LIBUSB_SUCCESS) {
        logger.error(std::format("Could not register hotplug callback: {}", libusb_error_name(result)));
        return;
    }

    hotplug_registered = true;
    logger.debug("Hotplug monitoring initialized");
}

USBHandler::~USBHandler() {
    if (!context) {
        return;
    }

    if (hotplug_registered) {
        libusb_hotplug_deregister_callback(context, callback_handle);
    }

    libusb_exit(context);
}

void USBHandler::handle() {
    if (!context) {
        return;
    }

    dispatch_hotplug_events();
    timeval timeout{0, 100000};
    const int result = libusb_handle_events_timeout_completed(context, &timeout, nullptr);

    if (result != LIBUSB_SUCCESS && result != LIBUSB_ERROR_INTERRUPTED) {
        logger.warning(std::format("Event handling failed: {}", libusb_error_name(result)));
    }

    dispatch_hotplug_events();
}

int USBHandler::hotplug_callback(libusb_context *, libusb_device *device, const libusb_hotplug_event event, void *obj) {
    const auto object = static_cast<USBHandler *>(obj);
    libusb_device_descriptor desc{};

    if (libusb_get_device_descriptor(device, &desc) != LIBUSB_SUCCESS) {
        object->logger.warning("Could not read USB device descriptor");
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

    // Device setup and teardown may use synchronous USB transfers or join the read thread.
    const std::lock_guard lock(object->pending_events_mutex);
    object->pending_events.emplace_back(clean_device, event);
    return 0;
}

void USBHandler::dispatch_hotplug_events() {
    std::vector<std::pair<Device, libusb_hotplug_event>> events;

    {
        const std::lock_guard lock(pending_events_mutex);
        events.swap(pending_events);
    }

    for (const auto &[device, event]: events) {
        const std::string details = std::format("bus {}, address {}, ID {:04X}:{:04X}", device.bus, device.address, device.vendor_id, device.device_id);

        if (event == LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED) {
            logger.info(std::format("Device connected: {}", details));

            if (connect_callback) {
                connect_callback(device);
            }
        } else if (event == LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT) {
            logger.info(std::format("Device disconnected: {}", details));

            if (disconnect_callback) {
                disconnect_callback(device);
            }
        }
    }
}

libusb_device_handle *USBHandler::get_device_handle(const Device device) const {
    if (!context) {
        return nullptr;
    }

    libusb_device **list = nullptr;
    const ssize_t count = libusb_get_device_list(context, &list);

    if (count < 0) {
        logger.error(std::format("Could not get USB device list: {}", libusb_error_name(static_cast<int>(count))));
        return nullptr;
    }

    libusb_device_handle *handle = nullptr;

    for (ssize_t i = 0; i < count; i++) {
        libusb_device *dev = list[i];

        if (libusb_get_bus_number(dev) != device.bus || libusb_get_device_address(dev) != device.address) {
            continue;
        }

        libusb_device_descriptor descriptor{};

        if (libusb_get_device_descriptor(dev, &descriptor) != LIBUSB_SUCCESS || descriptor.idVendor != device.vendor_id || descriptor.idProduct != device.device_id) {
            continue;
        }

        const int open_result = libusb_open(dev, &handle);

        if (open_result != LIBUSB_SUCCESS) {
            logger.error(std::format("Could not open device on bus {}, address {}: {}", device.bus, device.address, libusb_error_name(open_result)));
            handle = nullptr;
            break;
        }

        const int detach_result = libusb_set_auto_detach_kernel_driver(handle, 1);

        if (detach_result != LIBUSB_SUCCESS && detach_result != LIBUSB_ERROR_NOT_SUPPORTED) {
            logger.warning(std::format("Could not enable automatic kernel driver detach: {}", libusb_error_name(detach_result)));
        }

        const int claim_result = libusb_claim_interface(handle, 0);

        if (claim_result == LIBUSB_SUCCESS) {
            logger.info(std::format("Device ready on bus {}, address {}, interface 0 claimed", device.bus, device.address));
            break;
        }

        logger.error(std::format("Could not claim interface 0 on bus {}, address {}: {}", device.bus, device.address, libusb_error_name(claim_result)));
        libusb_close(handle);
        handle = nullptr;
        break;
    }

    libusb_free_device_list(list, 1);

    if (!handle) {
        logger.error(std::format("No usable handle for device on bus {}, address {}", device.bus, device.address));
    }

    return handle;
}
