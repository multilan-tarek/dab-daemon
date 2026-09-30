//
// Created by tarek on 9/30/26.
//

#ifndef USB_HANDLER_H
#define USB_HANDLER_H

#include <functional>
#include <libusb-1.0/libusb.h>

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

    void handle() const;

    libusb_device_handle *get_device_handle(Device device) const;

private:
    libusb_context *context = nullptr;
    libusb_hotplug_callback_handle callback_handle{};

    std::function<void(Device)> connect_callback = nullptr;
    std::function<void(Device)> disconnect_callback = nullptr;

    static int hotplug_callback(libusb_context *, libusb_device *device, libusb_hotplug_event event, void *obj);
};

#endif //USB_HANDLER_H
