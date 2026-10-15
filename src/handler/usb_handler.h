#ifndef USB_HANDLER_H
#define USB_HANDLER_H

#include <functional>
#include <libusb-1.0/libusb.h>
#include <mutex>
#include <utility>
#include <vector>
#include "../helper/logger.h"

class USBHandler {
public:
    struct Device {
        uint16_t vendor_id;
        uint16_t device_id;
        uint8_t bus;
        uint8_t address;

        bool operator==(const Device &) const = default;
    };

    explicit USBHandler(const std::function<void(Device)> &connect_callback, const std::function<void(Device)> &disconnect_callback);

    ~USBHandler();

    void init();

    void handle();

    libusb_device_handle *get_device_handle(Device device) const;

private:
    libusb_context *context = nullptr;
    libusb_hotplug_callback_handle callback_handle{};
    bool hotplug_registered = false;
    std::vector<std::pair<Device, libusb_hotplug_event>> pending_events;
    std::mutex pending_events_mutex;

    std::function<void(Device)> connect_callback = nullptr;
    std::function<void(Device)> disconnect_callback = nullptr;
    Logger logger{"USB"};

    void dispatch_hotplug_events();

    static int hotplug_callback(libusb_context *, libusb_device *device, libusb_hotplug_event event, void *obj);
};

#endif //USB_HANDLER_H
