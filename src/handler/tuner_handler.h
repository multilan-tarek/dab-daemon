//
// Created by tarek on 9/30/26.
//

#ifndef TUNER_HANDLER_H
#define TUNER_HANDLER_H

#include <libusb-1.0/libusb.h>
#include "../helper/tuner_helper.h"

class TunerHandler {
public:
    explicit TunerHandler(libusb_device_handle *device_handle);

    ~TunerHandler();

private:
    libusb_device_handle *device_handle = nullptr;
    TunerHelper *helper = nullptr;
};

#endif //TUNER_HANDLER_H
