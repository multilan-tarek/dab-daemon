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

include(CTest)

if(BUILD_TESTING)
    if(DAB_ENABLE_DIAGNOSTICS AND (DAB_ENABLE_AAC OR DAB_ENABLE_MP2))
        add_executable(dab_audio_tests tests/audio_statistics_test.cpp)
        target_include_directories(dab_audio_tests PRIVATE src/decoder src)
        target_compile_definitions(dab_audio_tests PRIVATE $<TARGET_PROPERTY:dab_deamon,COMPILE_DEFINITIONS>)
        target_link_libraries(dab_audio_tests PRIVATE Threads::Threads)

        if(DAB_ENABLE_AAC)
            target_sources(dab_audio_tests PRIVATE src/decoder/msc_decoder.cpp src/decoder/fec/decode_rs_char.c src/decoder/fec/init_rs_char.c)
        endif()

        if(DAB_ENABLE_MP2)
            target_sources(dab_audio_tests PRIVATE src/decoder/mp2_decoder.cpp)
        endif()

        add_test(NAME audio_statistics COMMAND dab_audio_tests)
    endif()

    add_executable(dab_usb_tests
            tests/usb_hotplug_test.cpp
            src/handler/usb_handler.cpp
            src/handler/main_handler.cpp
            src/helper/logger.cpp
    )
    target_include_directories(dab_usb_tests PRIVATE src/handler)
    target_link_libraries(dab_usb_tests PRIVATE PkgConfig::LIBUSB PkgConfig::SDBUSCPP Threads::Threads)
    find_program(DBUS_RUN_SESSION dbus-run-session)

    if(DBUS_RUN_SESSION)
        add_test(NAME usb_hotplug COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:dab_usb_tests>)
    endif()

    add_executable(dab_tuner_tests
            tests/tuner_background_test.cpp
            src/helper/tuner_helper.cpp
            src/helper/logger.cpp
    )
    target_include_directories(dab_tuner_tests PRIVATE src/helper)
    target_link_libraries(dab_tuner_tests PRIVATE PkgConfig::LIBUSB Threads::Threads)
    add_test(NAME tuner_background COMMAND dab_tuner_tests)

    add_executable(dab_fic_tests
            tests/fic_label_test.cpp
            src/decoder/fic_decoder.cpp
    )
    target_include_directories(dab_fic_tests PRIVATE src/decoder)
    target_compile_definitions(dab_fic_tests PRIVATE $<TARGET_PROPERTY:dab_deamon,COMPILE_DEFINITIONS>)
    add_test(NAME fic_labels COMMAND dab_fic_tests)

    if(DAB_ENABLE_DIAGNOSTICS)
        add_executable(dab_fic_inspector_tests
                tests/fic_inspector_test.cpp
                src/decoder/fic_decoder.cpp
        )
        target_include_directories(dab_fic_inspector_tests PRIVATE src/decoder)
        target_compile_definitions(dab_fic_inspector_tests PRIVATE $<TARGET_PROPERTY:dab_deamon,COMPILE_DEFINITIONS>)
        target_link_libraries(dab_fic_inspector_tests PRIVATE Threads::Threads)
        add_test(NAME fic_inspector COMMAND dab_fic_inspector_tests)
    endif()

    if(DBUS_RUN_SESSION)
        find_program(DAB_TEST_PYTHON NAMES python3 HINTS "${CMAKE_CURRENT_SOURCE_DIR}/.venv/bin")

        if(DAB_TEST_PYTHON)
            execute_process(COMMAND ${DAB_TEST_PYTHON} -c "import dbus_next" RESULT_VARIABLE DAB_TEST_DBUS_NEXT OUTPUT_QUIET ERROR_QUIET)

            if(DAB_TEST_DBUS_NEXT EQUAL 0)
                add_library(dab_background_usb_mock SHARED tests/background_usb_mock.cpp)
                target_link_libraries(dab_background_usb_mock PRIVATE PkgConfig::LIBUSB Threads::Threads)
                add_test(NAME dbus_concurrency COMMAND ${DBUS_RUN_SESSION} -- ${DAB_TEST_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/tests/dbus_concurrency_integration.py --binary $<TARGET_FILE:dab_deamon> --usb-mock $<TARGET_FILE:dab_background_usb_mock>)
                set_tests_properties(dbus_concurrency PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77 RESOURCE_LOCK dab_daemon_runtime ENVIRONMENT "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR};PYTHONDONTWRITEBYTECODE=1")

                if(DAB_ENABLE_AAC AND (DAB_ENABLE_FAST_SCAN OR DAB_ENABLE_FULL_SCAN))
                    if(DAB_ENABLE_FAST_SCAN)
                        set(DAB_SCAN_TEST_MODE fast)
                    else()
                        set(DAB_SCAN_TEST_MODE full)
                    endif()

                    add_test(NAME scan_selection COMMAND ${DBUS_RUN_SESSION} -- ${DAB_TEST_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/tests/scan_selection_integration.py --binary $<TARGET_FILE:dab_deamon> --usb-mock $<TARGET_FILE:dab_background_usb_mock> --mode ${DAB_SCAN_TEST_MODE})
                    set_tests_properties(scan_selection PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77 RESOURCE_LOCK dab_daemon_runtime ENVIRONMENT "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR};PYTHONDONTWRITEBYTECODE=1")
                endif()

                if(DAB_ENABLE_TPEG AND DAB_ENABLE_AAC)
                    set(DAB_TPEG_TEST_ARGUMENTS --binary $<TARGET_FILE:dab_deamon> --usb-mock $<TARGET_FILE:dab_background_usb_mock>)

                    if(DAB_ENABLE_SPI)
                        list(APPEND DAB_TPEG_TEST_ARGUMENTS --spi-enabled)
                    endif()

                    add_test(NAME tpeg_background COMMAND ${DBUS_RUN_SESSION} -- ${DAB_TEST_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/tests/tpeg_background_integration.py ${DAB_TPEG_TEST_ARGUMENTS})
                    set_tests_properties(tpeg_background PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77 RESOURCE_LOCK dab_daemon_runtime ENVIRONMENT "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR};PYTHONDONTWRITEBYTECODE=1")
                endif()
            endif()
        endif()
    endif()
endif()

if(BUILD_TESTING AND DAB_ENABLE_SLIDESHOW)
    add_executable(dab_slideshow_tests
            tests/pad_slideshow_test.cpp
            src/decoder/pad_decoder.cpp
            src/decoder/slideshow_decoder.cpp
    )
    target_include_directories(dab_slideshow_tests PRIVATE src/decoder)
    target_compile_definitions(dab_slideshow_tests PRIVATE DAB_ENABLE_DIAGNOSTICS=$<BOOL:${DAB_ENABLE_DIAGNOSTICS}>)
    add_test(NAME pad_slideshow COMMAND dab_slideshow_tests)
endif()

if(BUILD_TESTING AND DAB_ENABLE_SPI)
    add_executable(dab_mot_tests
            tests/mot_decoder_test.cpp
            src/decoder/mot_decoder.cpp
    )
    target_include_directories(dab_mot_tests PRIVATE src/decoder)
    target_compile_definitions(dab_mot_tests PRIVATE DAB_ENABLE_DIAGNOSTICS=$<BOOL:${DAB_ENABLE_DIAGNOSTICS}>)
    target_link_libraries(dab_mot_tests PRIVATE ZLIB::ZLIB)
    add_test(NAME mot_carousel COMMAND dab_mot_tests)
endif()

if(BUILD_TESTING AND DAB_ENABLE_SPI AND DAB_ENABLE_LOGO_CACHE)
    add_executable(dab_logo_tests
            tests/logo_cache_test.cpp
            src/helper/logo_cache.cpp
            src/helper/logo_helper.cpp
            src/helper/logger.cpp
    )
    target_include_directories(dab_logo_tests PRIVATE src/helper)
    target_link_libraries(dab_logo_tests PRIVATE ZLIB::ZLIB Threads::Threads)
    add_test(NAME logo_cache COMMAND dab_logo_tests)
endif()
