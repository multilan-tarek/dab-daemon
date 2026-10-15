#ifndef TUNER_HELPER_H
#define TUNER_HELPER_H

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <vector>
#include <libusb-1.0/libusb.h>
#include "logger.h"
#include "signal_metrics.h"

class TunerHelper {
public:
    struct USBStatus {
        int32_t last_error = LIBUSB_SUCCESS;
        int64_t last_error_time = 0;
        uint64_t errors = 0;
        uint64_t timeouts = 0;
    };

    class Operation {
    public:
        Operation(const TunerHelper &helper, std::chrono::milliseconds timeout);

        ~Operation();

        Operation(const Operation &) = delete;

        Operation &operator=(const Operation &) = delete;

        bool succeeded() const;

    private:
        const TunerHelper &helper;
        bool previous_active;
        std::chrono::steady_clock::time_point previous_deadline;
    };

    static constexpr auto INITIALIZATION_TIMEOUT = std::chrono::seconds(5);
    static constexpr auto TUNING_TIMEOUT = std::chrono::seconds(2);
    static constexpr auto RECEIVE_TIMEOUT = std::chrono::seconds(1);
    static constexpr auto RECOVERY_TIMEOUT = std::chrono::seconds(7);

    struct SignalStatus {
        bool ofdm_lock = false;
        bool fec_lock = false;
        bool fec_sync = false;
        bool msc_cer_valid = false;
        uint8_t antenna_level = 0;
        uint32_t msc_cer = 0;

        bool operator==(const SignalStatus &other) const = default;
    };

    constexpr static int FREQ_START = 174928;
    constexpr static int FREQ_STEP = 1712;

    constexpr static unsigned PLL_MIN_SETTLE_MS = 20;
    constexpr static unsigned PLL_POLL_MS = 5;
    constexpr static unsigned PLL_TIMEOUT_MS = 500;

    enum RegisterPage : uint8_t {
        REGISTER_PAGE_OFDM = 0x02, //For 1seg
        REGISTER_PAGE_FEC = 0x03, //For 1seg
        REGISTER_PAGE_COMM = 0x04,
        REGISTER_PAGE_FM = 0x06, //T-DMB OFDM/FM
        REGISTER_PAGE_HOST = 0x07,
        REGISTER_PAGE_CAS = 0x08,
        REGISTER_PAGE_DD = 0x09, //FEC for T-DMB, DAB, FM
        REGISTER_PAGE_FIC = 0x0A,
        REGISTER_PAGE_MSC0 = 0x0B,
        REGISTER_PAGE_MSC1 = 0x0C,
        REGISTER_PAGE_RF = 0x0F
    };

    enum Endpoint : uint8_t {
        ENDPOINT_OUT = 0x02,
        ENDPOINT_IN = 0x82
    };

    enum ADCClockType : uint8_t {
        ADC_CLOCK_FREQ_8_MHz = 0,
        ADC_CLOCK_FREQ_8_192_MHz = 1,
        ADC_CLOCK_FREQ_9_MHz = 2,
        ADC_CLOCK_FREQ_9_6_MHz = 3
    };

    // Values for ADC_CLOCK_FREQ_* enums
    constexpr static uint8_t ADC_CLOCK_CONFIG[4][7] = {
        {0x08, 0x01, 0x13, 0x25, 0x06, 0x7D, 0xB4}, // Based 24.576MHz,	8MHz	   External Clock21
        {0x06, 0x01, 0x0F, 0x27, 0x07, 0x60, 0xB8}, // Based 24.576MHz,	8.192MHz   External Clock20
        {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, // Based 24.576MHz,   9MHz       Unsupported Clock
        {0x0C, 0x01, 0x1F, 0x27, 0x06, 0xE1, 0xB8} // Based 24.576MHz,	9.6MHz	   External Clock22
    };

    static constexpr uint8_t ADC_CLOCK_ENSEMBLE_MAP[]{
        ADC_CLOCK_FREQ_8_192_MHz, //5A : 174928,
        ADC_CLOCK_FREQ_9_6_MHz, //5B : 176640,
        ADC_CLOCK_FREQ_8_192_MHz, //5C : 178352,
        ADC_CLOCK_FREQ_9_6_MHz, //5D : 180064,
        ADC_CLOCK_FREQ_8_192_MHz, //6A : 181936,
        ADC_CLOCK_FREQ_9_6_MHz, //6B : 183648,
        ADC_CLOCK_FREQ_8_192_MHz, //6C : 185360,
        ADC_CLOCK_FREQ_8_192_MHz, //6D : 187072,
        ADC_CLOCK_FREQ_8_MHz, //7A : 188928,
        ADC_CLOCK_FREQ_8_192_MHz, //7B : 190640,
        ADC_CLOCK_FREQ_8_192_MHz, //7C : 192352,
        ADC_CLOCK_FREQ_8_192_MHz, //7D : 194064,
        ADC_CLOCK_FREQ_8_MHz, //8A : 195936,
        ADC_CLOCK_FREQ_8_192_MHz, //8B : 197648,
        ADC_CLOCK_FREQ_8_192_MHz, //8C : 199360,
        ADC_CLOCK_FREQ_8_MHz, //8D : 201072,
        ADC_CLOCK_FREQ_8_192_MHz, //9A : 202928,
        ADC_CLOCK_FREQ_8_MHz, //9B : 204640,
        ADC_CLOCK_FREQ_8_192_MHz, //9C : 206352,
        ADC_CLOCK_FREQ_9_6_MHz, //9D : 208064,
        ADC_CLOCK_FREQ_8_192_MHz, //10A: 209936,
        ADC_CLOCK_FREQ_8_192_MHz, //10N: 210096,
        ADC_CLOCK_FREQ_8_192_MHz, //10B: 211648,
        ADC_CLOCK_FREQ_8_MHz, //10C: 213360,
        ADC_CLOCK_FREQ_8_192_MHz, //10D: 215072,
        ADC_CLOCK_FREQ_9_6_MHz, //11A: 216928,
        ADC_CLOCK_FREQ_8_192_MHz, //11N: 217088,
        ADC_CLOCK_FREQ_8_192_MHz, //11B: 218640,
        ADC_CLOCK_FREQ_8_192_MHz, //11C: 220352,
        ADC_CLOCK_FREQ_8_MHz, //11D: 222064,
        ADC_CLOCK_FREQ_8_192_MHz, //12A: 223936,
        ADC_CLOCK_FREQ_8_MHz, //12N: 224096,
        ADC_CLOCK_FREQ_8_MHz, //12B: 225648,
        ADC_CLOCK_FREQ_8_192_MHz, //12C: 227360,
        ADC_CLOCK_FREQ_8_MHz, //12D: 229072,
        ADC_CLOCK_FREQ_8_192_MHz, //13A: 230784,
        ADC_CLOCK_FREQ_8_192_MHz, //13B: 232496,
        ADC_CLOCK_FREQ_8_MHz, //13C: 234208,
        ADC_CLOCK_FREQ_8_192_MHz, //13D: 235776,
        ADC_CLOCK_FREQ_8_MHz, //13E: 237488,
        ADC_CLOCK_FREQ_8_192_MHz, //13F: 239200
    };

    static constexpr int PLL_NF_ENSEMBLE_MAP[]{
        0x38F3D55, // 5A : 174928,
        0x3982800, // 5B : 176640,
        0x3A112AA, // 5C : 178352,
        0x3A9FD55, // 5D : 180064,
        0x3B3BD55, // 6A : 181936,
        0x3BCA800, // 6B : 183648,
        0x3C592AA, // 6C : 185360,
        0x3CE7D55, // 6D : 187072,
        0x3D82800, // 7A : 188928,
        0x3E112AA, // 7B : 190640,
        0x3E9FD55, // 7C : 192352,
        0x3F2E800, // 7D : 194064,
        0x3FCA800, // 8A : 195936,
        0x40592AA, // 8B : 197648,
        0x40E7D55, // 8C : 199360,
        0x4176800, // 8D : 201072,
        0x42112AA, // 9A : 202928,
        0x429FD55, // 9B : 204640,
        0x432E800, // 9C : 206352,
        0x43BD2AA, // 9D : 208064,
        0x44592AA, // 10A: 209936,
        0x4466800, // 10N: 210096,
        0x44E7D55, // 10B: 211648,
        0x4576800, // 10C: 213360,
        0x46052AA, // 10D: 215072,
        0x469FD55, // 11A: 216928,
        0x46AD2AA, // 11N: 217088,
        0x472E800, // 11B: 218640,
        0x47BD2AA, // 11C: 220352,
        0x484BD55, // 11D: 222064,
        0x48E7D55, // 12A: 223936,
        0x48F52AA, // 12N: 224096,
        0x4976800, // 12B: 225648,
        0x4A052AA, // 12C: 227360,
        0x4A93D55, // 12D: 229072,
        0x4B22800, // 13A: 230784,
        0x4BB12AA, // 13B: 232496,
        0x4C3FD55, // 13C: 234208,
        0x4CC2800, // 13D: 235776,
        0x4D512AA, // 13E: 237488,
        0x4DDFD55 // 13F: 239200
    };

    static constexpr uint8_t MSC1_E_OVER_FLOW = 0x40;
    static constexpr uint8_t MSC1_E_UNDER_FLOW = 0x20;
    static constexpr uint8_t MSC1_E_INT = 0x10;
    static constexpr uint8_t MSC0_E_OVER_FLOW = 0x08;
    static constexpr uint8_t MSC0_E_UNDER_FLOW = 0x04;
    static constexpr uint8_t MSC0_E_INT = 0x02;
    static constexpr uint8_t FIC_E_INT = 0x01;
    static constexpr uint8_t RE_CONFIG_E_INT = 0x04;

    static constexpr uint8_t MSC1_INTR_BITS = (MSC1_E_INT | MSC1_E_UNDER_FLOW | MSC1_E_OVER_FLOW);
    static constexpr uint8_t MSC0_INTR_BITS = (MSC0_E_INT | MSC0_E_UNDER_FLOW | MSC0_E_OVER_FLOW);

    static constexpr uint8_t INT_E_UCLRL = 0x35; /// [2]MSC1 int clear [1]MSC0 int clear [0]FIC int clear
    static constexpr uint8_t INT_E_UCLRH = 0x36; /// [6]OFDM TII done clear

    static constexpr uint8_t INT_E_STATL = 0x33; /// [7]OFDM Lock status [6]MSC1 overrun [5]MSC1 underrun [4]MSC1 int [3]MSC0 overrun [2]MSC0 underrun [1]MSC0 int [0]FIC int
    static constexpr uint8_t INT_E_STATH = 0x34; /// [7]OFDM NIS [6]OFDM TII [5]OFDM scan [4]OFDM window position [3]OFDM unlock [2]FEC re-configuration [1]FEC CIF end [0]FEC soft reset

    static constexpr uint8_t RTV_DAB_OFDM_LOCK_MASK = 0x01;
    static constexpr uint8_t RTV_DAB_FEC_LOCK_MASK = 0x02;
    static constexpr uint8_t RTV_DAB_CHANNEL_LOCK_OK = (RTV_DAB_OFDM_LOCK_MASK | RTV_DAB_FEC_LOCK_MASK);

    explicit TunerHelper(int tuner_id, libusb_device_handle *device_handle, const std::function<void(const std::vector<uint8_t> &)> &fic_callback, const std::function<void(const std::vector<uint8_t> &)> &msc_callback, const std::function<void(const std::vector<uint8_t> &)> &background_msc_callback = nullptr);

    void switch_page(RegisterPage register_page) const;

    void set_register(uint8_t reg, uint8_t value) const;

    uint8_t read_register(uint8_t reg) const;

    bool power_up() const;

    bool set_frequency(uint32_t frequency);

    void open_subchannel(uint8_t subchannel);

    void open_background_subchannel(uint8_t subchannel);

    void close_background_subchannel();

    void read_data() const;

    SignalStatus get_signal_status() const;

    int take_usb_error() const;

    USBStatus get_usb_status() const;

    Operation begin_operation(std::chrono::milliseconds timeout) const;

    void cancel_operations();

private:
    libusb_device_handle *device_handle = nullptr;

    std::function<void(const std::vector<uint8_t> &)> fic_callback = nullptr;
    std::function<void(const std::vector<uint8_t> &)> msc_callback = nullptr;
    std::function<void(const std::vector<uint8_t> &)> background_msc_callback = nullptr;

    uint8_t current_subchannel = 0xFF;
    uint8_t background_subchannel = 0xFF;
    Logger logger{"TUNER"};
    mutable std::atomic_int pending_usb_error{LIBUSB_SUCCESS};
    std::atomic_bool operations_cancelled = false;
    mutable std::atomic_bool device_lost = false;
    mutable bool operation_active = false;
    mutable int operation_error = LIBUSB_SUCCESS;
    mutable std::chrono::steady_clock::time_point operation_deadline;
    mutable std::mutex usb_status_mutex;
    mutable USBStatus usb_status;

    int bulk_transfer(uint8_t endpoint, std::vector<uint8_t> &buffer) const;

    int check_operation() const;

    void record_usb_error(int error) const;

    void configure_power_type() const;

    void configure_adc_clock() const;

    bool changed_adc_clock(uint8_t adc_clock_type) const;

    void tdmb_init_top() const;

    void tdmb_init_comm() const;

    void tdmb_init_host() const;

    void tdmb_init_of_dm() const;

    void tdmb_init_fec() const;

    void reset_memory_fic() const;

    void oem_configure_interrupt() const;

    void stream_disable() const;

    void configure_host_if() const;

    void rf_initialize() const;

    void rf_special() const;

    void setup_msc_threshold() const;

    void setup_memory_fic() const;

    void clear_and_setup_msc_memory() const;

    void clear_and_setup_msc0_memory() const;
};

#endif //TUNER_HELPER_H
