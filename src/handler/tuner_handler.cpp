//
// Created by tarek on 9/30/26.
//

#include "tuner_handler.h"
#include <iostream>
#include "../helper/tuner_helper.h"

TunerHandler::TunerHandler(libusb_device_handle *device_handle) {
    this->device_handle = device_handle;
    this->helper = new TunerHelper(device_handle);

    std::cout << "Tuner Handler: Initializing..." << std::endl;

    if (!helper->power_up()) {
        std::cerr << "Tuner Handler: Initialization failed" << std::endl;
        return;
    }
}

TunerHandler::~TunerHandler() {
    delete helper;
}

