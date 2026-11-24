# Building

## Requirements

- Linux
- C compiler
- C++20 compiler and standard library with std::format support
- CMake 4.2 or newer
- pkg-config
- GNU Make or Ninja
- sdbus-c++ 2.x development headers and libraries
- libusb 1.0 development headers and libraries
- POSIX thread support
- zlib development headers and libraries (only if SPI or Journaline is enabled)

## Build and run

Run these commands from the project root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/dab_deamon
```

Add `-G Ninja` to the configure command to use Ninja.

The executable is named `dab_deamon`.\
Pass `--debug` when starting it to enable detailed logging.

## Feature flags

All `DAB_ENABLE_*` options default to **ON**. Set them to `OFF` and rebuild
to disable features. The D-Bus interfaces and GUI follow the enabled features.

| CMake option | Enables |
| --- | --- |
| `DAB_ENABLE_AAC` | DAB+ AAC audio |
| `DAB_ENABLE_MP2` | DAB MPEG Layer II audio |
| `DAB_ENABLE_SLIDESHOW` | MOT SlideShow images and metadata |
| `DAB_ENABLE_SPI` | SPI service information, programme schedules, and image assets |
| `DAB_ENABLE_LOGO_CACHE` | Persistent caching of received station logos; requires SPI |
| `DAB_ENABLE_JOURNALINE` | Journaline text services and navigation data |
| `DAB_ENABLE_TPEG` | TPEG service directories and transport frames |
| `DAB_ENABLE_EWS` | Emergency warning signalling |
| `DAB_ENABLE_FIDC` | Fast Information Data Channel entries |
| `DAB_ENABLE_ANNOUNCEMENTS` | Announcement support and switching information |
| `DAB_ENABLE_SERVICE_LINKING` | Linked services and other-ensemble service references |
| `DAB_ENABLE_FREQUENCY_INFO` | Alternative frequency information |
| `DAB_ENABLE_SEEK` | Seek up/down to the next received Band III ensemble |
| `DAB_ENABLE_FAST_SCAN` | Fast Band III ensemble scanning |
| `DAB_ENABLE_FULL_SCAN` | Full Band III scans with service collection |
| `DAB_ENABLE_DIAGNOSTICS` | FIC inspector, hardware/data diagnostics, and audio statistics |

For example, disable the slideshow and diagnostics:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DAB_ENABLE_DIAGNOSTICS=OFF -DAB_ENABLE_SLIDESHOW=OFF
cmake --build build --parallel
```

- Turning off both audio codecs removes the audio interface.
- Seek remains available when both fast/full scan modes are disabled.
- Turning off seek and both scan modes removes the scan interface.
- Disabling diagnostics keeps normal reception and basic signal readings available.
- Cache files already on disk are retained when logo caching is disabled.
