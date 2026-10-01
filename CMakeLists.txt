cmake_minimum_required(VERSION 4.2)
project(dab_deamon)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
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
        src/decoder/fic_decoder.cpp
        src/decoder/fic_decoder.h
        src/decoder/msc_decoder.cpp
        src/decoder/msc_decoder.h
        src/decoder/pad_decoder.cpp
        src/decoder/pad_decoder.h
        src/decoder/dynamic_label_decoder.cpp
        src/decoder/dynamic_label_decoder.h
        src/decoder/slideshow_decoder.cpp
        src/decoder/slideshow_decoder.h
        src/decoder/fec/decode_rs_char.c
        src/decoder/fec/init_rs_char.c
        src/encoder/loas_encoder.cpp
        src/encoder/loas_encoder.h
        src/helper/audio_pipe.cpp
        src/helper/audio_pipe.h
        src/helper/slideshow_file.cpp
        src/helper/slideshow_file.h
        src/helper/logger.cpp
        src/helper/logger.h
)

target_link_libraries(dab_deamon PRIVATE
        PkgConfig::SDBUSCPP
        PkgConfig::LIBUSB
        Threads::Threads
)