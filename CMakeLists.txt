cmake_minimum_required(VERSION 4.2)
project(dab_deamon)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(DAB_ENABLE_AAC "Build DAB+ AAC audio support" ON)
option(DAB_ENABLE_MP2 "Build DAB MPEG Layer II audio support" ON)
option(DAB_ENABLE_SLIDESHOW "Build MOT SlideShow support" ON)
option(DAB_ENABLE_SPI "Build SPI/EPG support" ON)
option(DAB_ENABLE_LOGO_CACHE "Cache received station logos on disk" ON)
option(DAB_ENABLE_JOURNALINE "Build Journaline support" ON)
option(DAB_ENABLE_TPEG "Build TPEG support" ON)
option(DAB_ENABLE_EWS "Build Emergency Warning System support" ON)
option(DAB_ENABLE_FIDC "Build FIG Type 5 / FIDC support" ON)
option(DAB_ENABLE_ANNOUNCEMENTS "Build announcement support/signalling" ON)
option(DAB_ENABLE_SERVICE_LINKING "Build service linking support" ON)
option(DAB_ENABLE_FREQUENCY_INFO "Build frequency information support" ON)
option(DAB_ENABLE_SEEK "Build directional Band III ensemble seeking" ON)
option(DAB_ENABLE_FAST_SCAN "Build fast scan support" ON)
option(DAB_ENABLE_FULL_SCAN "Build full scan support" ON)
option(DAB_ENABLE_DIAGNOSTICS "Build diagnostics/statistics D-Bus support" ON)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(SDBUSCPP REQUIRED IMPORTED_TARGET sdbus-c++)
pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)

set(DAB_SOURCES
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
        src/config/features.h
        src/helper/tuner_helper.cpp
        src/helper/tuner_helper.h
        src/helper/ensemble_cache.cpp
        src/helper/ensemble_cache.h
        src/helper/signal_quality_filter.cpp
        src/helper/signal_quality_filter.h
        src/helper/signal_metrics.h
        src/helper/dab_channels.h
        src/decoder/fic_decoder.cpp
        src/decoder/fic_decoder.h
        src/decoder/pad_decoder.cpp
        src/decoder/pad_decoder.h
        src/decoder/dynamic_label_decoder.cpp
        src/decoder/dynamic_label_decoder.h
        src/helper/logger.cpp
        src/helper/logger.h
)

if(DAB_ENABLE_AAC)
    list(APPEND DAB_SOURCES
            src/decoder/msc_decoder.cpp
            src/decoder/msc_decoder.h
            src/encoder/loas_encoder.cpp
            src/encoder/loas_encoder.h
    )
endif()

if(DAB_ENABLE_AAC OR DAB_ENABLE_SPI OR DAB_ENABLE_JOURNALINE OR DAB_ENABLE_TPEG)
    list(APPEND DAB_SOURCES
            src/decoder/fec/decode_rs_char.c
            src/decoder/fec/init_rs_char.c
    )
endif()

if(DAB_ENABLE_MP2)
    list(APPEND DAB_SOURCES
            src/decoder/mp2_decoder.cpp
            src/decoder/mp2_decoder.h
    )
endif()

if(DAB_ENABLE_AAC OR DAB_ENABLE_MP2)
    list(APPEND DAB_SOURCES
            src/helper/audio_pipe.cpp
            src/helper/audio_pipe.h
            src/helper/audio_mute.cpp
            src/helper/audio_mute.h
            src/helper/audio_statistics.h
    )
endif()

if(DAB_ENABLE_SLIDESHOW)
    list(APPEND DAB_SOURCES
            src/decoder/slideshow_decoder.cpp
            src/decoder/slideshow_decoder.h
            src/helper/slideshow_file.cpp
            src/helper/slideshow_file.h
    )
endif()

if(DAB_ENABLE_SPI OR DAB_ENABLE_JOURNALINE OR DAB_ENABLE_TPEG)
    list(APPEND DAB_SOURCES
            src/decoder/packet_decoder.cpp
            src/decoder/packet_decoder.h
    )
endif()

if(DAB_ENABLE_SPI)
    list(APPEND DAB_SOURCES
            src/decoder/mot_decoder.cpp
            src/decoder/mot_decoder.h
            src/decoder/spi_decoder.cpp
            src/decoder/spi_decoder.h
            src/helper/spi_file.cpp
            src/helper/spi_file.h
            src/helper/logo_helper.cpp
            src/helper/logo_helper.h
            src/helper/spi_group_helper.cpp
            src/helper/spi_group_helper.h
    )
endif()

if(DAB_ENABLE_JOURNALINE)
    list(APPEND DAB_SOURCES
            src/decoder/journaline_decoder.cpp
            src/decoder/journaline_decoder.h
    )
endif()

if(DAB_ENABLE_SPI AND DAB_ENABLE_LOGO_CACHE)
    list(APPEND DAB_SOURCES
            src/helper/logo_cache.cpp
            src/helper/logo_cache.h
    )
endif()

if(DAB_ENABLE_TPEG)
    list(APPEND DAB_SOURCES
            src/decoder/tpeg_decoder.cpp
            src/decoder/tpeg_decoder.h
            src/decoder/tpeg_content_decoder.cpp
            src/decoder/tpeg_content_decoder.h
    )
endif()

add_executable(dab_deamon ${DAB_SOURCES})

target_compile_definitions(dab_deamon PRIVATE
        DAB_ENABLE_AAC=$<BOOL:${DAB_ENABLE_AAC}>
        DAB_ENABLE_MP2=$<BOOL:${DAB_ENABLE_MP2}>
        DAB_ENABLE_SLIDESHOW=$<BOOL:${DAB_ENABLE_SLIDESHOW}>
        DAB_ENABLE_SPI=$<BOOL:${DAB_ENABLE_SPI}>
        DAB_ENABLE_LOGO_CACHE=$<BOOL:${DAB_ENABLE_LOGO_CACHE}>
        DAB_ENABLE_JOURNALINE=$<BOOL:${DAB_ENABLE_JOURNALINE}>
        DAB_ENABLE_TPEG=$<BOOL:${DAB_ENABLE_TPEG}>
        DAB_ENABLE_EWS=$<BOOL:${DAB_ENABLE_EWS}>
        DAB_ENABLE_FIDC=$<BOOL:${DAB_ENABLE_FIDC}>
        DAB_ENABLE_ANNOUNCEMENTS=$<BOOL:${DAB_ENABLE_ANNOUNCEMENTS}>
        DAB_ENABLE_SERVICE_LINKING=$<BOOL:${DAB_ENABLE_SERVICE_LINKING}>
        DAB_ENABLE_FREQUENCY_INFO=$<BOOL:${DAB_ENABLE_FREQUENCY_INFO}>
        DAB_ENABLE_SEEK=$<BOOL:${DAB_ENABLE_SEEK}>
        DAB_ENABLE_FAST_SCAN=$<BOOL:${DAB_ENABLE_FAST_SCAN}>
        DAB_ENABLE_FULL_SCAN=$<BOOL:${DAB_ENABLE_FULL_SCAN}>
        DAB_ENABLE_DIAGNOSTICS=$<BOOL:${DAB_ENABLE_DIAGNOSTICS}>
)

target_link_libraries(dab_deamon PRIVATE
        PkgConfig::SDBUSCPP
        PkgConfig::LIBUSB
        Threads::Threads
)

if(DAB_ENABLE_SPI OR DAB_ENABLE_JOURNALINE)
    find_package(ZLIB REQUIRED)
    target_link_libraries(dab_deamon PRIVATE ZLIB::ZLIB)
endif()
