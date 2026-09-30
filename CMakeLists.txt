cmake_minimum_required(VERSION 4.2)
project(dab_deamon)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDBUSCPP REQUIRED IMPORTED_TARGET sdbus-c++)
pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)

add_executable(dab_deamon
        src/main.cpp
        src/handler/usb_handler.cpp
        src/handler/usb_handler.h
        src/handler/main_handler.cpp
        src/handler/main_handler.h
        src/helper/partial.h
        src/handler/tuner_handler.cpp
        src/handler/tuner_handler.h
        src/handler/dbus_handler.cpp
        src/handler/dbus_handler.h
        src/helper/tuner_helper.cpp
        src/helper/tuner_helper.h
)

target_link_libraries(dab_deamon PRIVATE
        PkgConfig::SDBUSCPP
        PkgConfig::LIBUSB
)