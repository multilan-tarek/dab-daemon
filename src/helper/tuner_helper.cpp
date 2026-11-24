#include "tuner_helper.h"
#include "dab_channels.h"
#include <algorithm>
#include <chrono>
#include <format>
#include <iterator>
#include <thread>
#include <vector>

static_assert(DAB_CHANNELS.size() == std::size(TunerHelper::ADC_CLOCK_ENSEMBLE_MAP));
static_assert(DAB_CHANNELS.size() == std::size(TunerHelper::PLL_NF_ENSEMBLE_MAP));

TunerHelper::Operation::Operation(const TunerHelper &helper, const std::chrono::milliseconds timeout)
    : helper(helper), previous_active(helper.operation_active), previous_deadline(helper.operation_deadline) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;

    if (!previous_active) {
        helper.operation_error = LIBUSB_SUCCESS;
    }

    helper.operation_deadline = previous_active ? std::min(previous_deadline, deadline) : deadline;
    helper.operation_active = true;
}

TunerHelper::Operation::~Operation() {
    helper.operation_deadline = previous_deadline;
    helper.operation_active = previous_active;
}

bool TunerHelper::Operation::succeeded() const {
    return helper.check_operation() == LIBUSB_SUCCESS;
}

TunerHelper::Operation TunerHelper::begin_operation(const std::chrono::milliseconds timeout) const {
    return Operation(*this, timeout);
}

void TunerHelper::cancel_operations() {
    operations_cancelled = true;
}

int TunerHelper::check_operation() const {
    if (operations_cancelled) {
        return LIBUSB_ERROR_INTERRUPTED;
    }

    if (device_lost) {
        return LIBUSB_ERROR_NO_DEVICE;
    }

    if (operation_active && operation_error != LIBUSB_SUCCESS) {
        return operation_error;
    }

    if (operation_active && std::chrono::steady_clock::now() >= operation_deadline) {
        record_usb_error(LIBUSB_ERROR_TIMEOUT);
        return LIBUSB_ERROR_TIMEOUT;
    }

    return LIBUSB_SUCCESS;
}

void TunerHelper::record_usb_error(const int error) const {
    if (operation_active) {
        operation_error = error;
    }

    int expected = LIBUSB_SUCCESS;
    pending_usb_error.compare_exchange_strong(expected, error);

    if (error == LIBUSB_ERROR_NO_DEVICE) {
        device_lost = true;
        pending_usb_error = error;
    }

    const std::lock_guard lock(usb_status_mutex);
    usb_status.last_error = error;
    usb_status.last_error_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    usb_status.errors++;

    if (error == LIBUSB_ERROR_TIMEOUT) {
        usb_status.timeouts++;
    }
}

TunerHelper::USBStatus TunerHelper::get_usb_status() const {
    const std::lock_guard lock(usb_status_mutex);
    return usb_status;
}

TunerHelper::TunerHelper(const int tuner_id, libusb_device_handle *device_handle, const std::function<void(const std::vector<uint8_t> &)> &fic_callback, const std::function<void(const std::vector<uint8_t> &)> &msc_callback, const std::function<void(const std::vector<uint8_t> &)> &background_msc_callback) {
    logger.set_id(tuner_id);
    this->device_handle = device_handle;
    this->fic_callback = fic_callback;
    this->msc_callback = msc_callback;
    this->background_msc_callback = background_msc_callback;
}

int TunerHelper::bulk_transfer(const uint8_t endpoint, std::vector<uint8_t> &buffer) const {
    const int operation_result = check_operation();

    if (operation_result != LIBUSB_SUCCESS) {
        return operation_result;
    }

    unsigned timeout = 1000;

    if (operation_active) {
        const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(operation_deadline - std::chrono::steady_clock::now());
        timeout = static_cast<unsigned>(std::clamp<int64_t>(remaining.count(), 1, timeout));
    }

    int transferred = 0;
    int result = libusb_bulk_transfer(device_handle, endpoint, buffer.data(), buffer.size(), &transferred, timeout);

    if (operations_cancelled) {
        return LIBUSB_ERROR_INTERRUPTED;
    }

    if (result == LIBUSB_SUCCESS && endpoint == ENDPOINT_OUT && transferred != static_cast<int>(buffer.size())) {
        result = LIBUSB_ERROR_IO;
    }

    if (result != LIBUSB_SUCCESS) {
        record_usb_error(result);

        if (result == LIBUSB_ERROR_NO_DEVICE) {
            logger.warning(std::format("USB device disappeared during transfer on endpoint 0x{:02X}", endpoint));
        } else if (result != LIBUSB_ERROR_TIMEOUT) {
            logger.warning(std::format("USB bulk transfer on endpoint 0x{:02X} failed: {}", endpoint, libusb_error_name(result)));
        }

        return result;
    }

    return transferred;
}

void TunerHelper::switch_page(const RegisterPage register_page) const {
    std::vector<uint8_t> data{0x21, 0x00, 0x00, 0x02, 0x03, static_cast<uint8_t>(register_page)};
    bulk_transfer(ENDPOINT_OUT, data);
}

void TunerHelper::set_register(const uint8_t reg, const uint8_t value) const {
    std::vector<uint8_t> data{0x21, 0x00, 0x00, 0x02, reg, value};
    bulk_transfer(ENDPOINT_OUT, data);
}

uint8_t TunerHelper::read_register(const uint8_t reg) const {
    std::vector<uint8_t> data{0x22, 0x00, 0x01, 0x00, reg};

    if (bulk_transfer(ENDPOINT_OUT, data) < 0) {
        return 0;
    }

    std::vector<uint8_t> read_buffer(5);

    const int transferred = bulk_transfer(ENDPOINT_IN, read_buffer);

    if (transferred < 5) {
        if (transferred >= 0) {
            record_usb_error(LIBUSB_ERROR_IO);
        }

        return 0;
    }

    return read_buffer[4];
}

bool TunerHelper::power_up() const {
    const auto operation = begin_operation(INITIALIZATION_TIMEOUT);
    const auto initialization_start = std::chrono::steady_clock::now();

    logger.info("Starting hardware initialization");
    logger.info("Checking host communication");

    for (int i = 0; i < 100; i++) {
        switch_page(REGISTER_PAGE_HOST);
        set_register(0x7D, 0x06);

        if (read_register(0x7D) != 0x06) {
            if (!operation.succeeded()) {
                logger.error("Hardware initialization aborted during host communication");
                return false;
            }

            continue;
        }

        logger.info("Host communication ready");
        logger.debug(std::format("Power-up handshake successful after {} attempt(s)", i + 1));

        auto phase_start = std::chrono::steady_clock::now();
        logger.info("Configuring power and ADC clock");
        logger.debug("Configuring power type");
        configure_power_type();
        logger.debug("Configuring ADC clock");
        configure_adc_clock();
        if (!operation.succeeded()) {
            return false;
        }
        logger.debug(std::format("Power and ADC clock configured in {} ms", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - phase_start).count()));

        phase_start = std::chrono::steady_clock::now();
        logger.info("Initializing DAB baseband");
        logger.debug("Initializing top block");
        tdmb_init_top();
        logger.debug("Initializing communication block");
        tdmb_init_comm();
        logger.debug("Initializing host block");
        tdmb_init_host();
        logger.debug("Initializing OFDM block");
        tdmb_init_of_dm();
        logger.debug("Initializing FEC block");
        tdmb_init_fec();
        if (!operation.succeeded()) {
            return false;
        }
        logger.debug(std::format("DAB baseband initialized in {} ms", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - phase_start).count()));

        phase_start = std::chrono::steady_clock::now();
        logger.info("Configuring host interface and interrupts");
        logger.debug("Resetting FIC memory");
        reset_memory_fic();
        logger.debug("Configuring interrupt output");
        oem_configure_interrupt();
        logger.debug("Disabling stream output during setup");
        stream_disable();
        logger.debug("Configuring host interface");
        configure_host_if();
        if (!operation.succeeded()) {
            return false;
        }
        logger.debug(std::format("Host interface and interrupts configured in {} ms", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - phase_start).count()));

        phase_start = std::chrono::steady_clock::now();
        logger.info("Initializing RF frontend");
        logger.debug("Writing RF initialization table");
        rf_initialize();
        logger.debug("Running RF special setup");
        rf_special();
        if (!operation.succeeded()) {
            return false;
        }
        logger.debug(std::format("RF frontend initialized in {} ms", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - phase_start).count()));

        phase_start = std::chrono::steady_clock::now();
        logger.info("Configuring FIC and MSC buffers");
        setup_msc_threshold();
        setup_memory_fic();
        if (!operation.succeeded()) {
            return false;
        }
        logger.debug(std::format("FIC and MSC buffers configured in {} ms", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - phase_start).count()));

        logger.info(std::format("Hardware initialization complete in {} ms", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - initialization_start).count()));
        return true;
    }

    logger.error(std::format("Power-up handshake failed after 100 attempts ({} ms)", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - initialization_start).count()));
    return false;
}

void TunerHelper::configure_power_type() const {
    constexpr uint8_t RTV_IO_1_8V = 0x02;
    constexpr uint8_t REG52 = 0x07;
    constexpr uint8_t REG54 = 0x1C;

    uint8_t REG30 = 0xF2 & 0xF0;
    REG30 = REG30 | RTV_IO_1_8V << 1;

    uint8_t REG2F = 0x61;
    REG2F = static_cast<uint8_t>(REG2F | 0x10);

    switch_page(REGISTER_PAGE_RF);

    set_register(0x54, REG54);
    set_register(0x52, REG52);
    set_register(0x30, REG30);
    set_register(0x2F, REG2F);
}

void TunerHelper::configure_adc_clock() const {
    constexpr uint8_t REGE8 = 0x46 & 0xC0;
    constexpr uint8_t REGE9 = 0x54 & 0xF0;
    constexpr uint8_t REGEA = 0x07 & 0xC0;
    constexpr uint8_t REGEB = 0x27 & 0xC0;
    constexpr uint8_t REGEC = 0x1E & 0xC0;
    constexpr uint8_t REGED = 0x18 & 0x00;
    constexpr uint8_t REGEE = 0xB8 & 0x00;

    switch_page(REGISTER_PAGE_RF);

    set_register(0xE8, REGE8 | ADC_CLOCK_CONFIG[0][0]);
    set_register(0xE9, REGE9 | ADC_CLOCK_CONFIG[0][1]);
    set_register(0xEA, REGEA | ADC_CLOCK_CONFIG[0][2]);
    set_register(0xEB, REGEB | ADC_CLOCK_CONFIG[0][3]);
    set_register(0xEC, REGEC | ADC_CLOCK_CONFIG[0][4]);
    set_register(0xED, REGED | ADC_CLOCK_CONFIG[0][5]);
    set_register(0xEE, REGEE | ADC_CLOCK_CONFIG[0][6]);

    bool locked = false;

    for (int i = 0; i < 10; i++) {
        if ((read_register(0x15) & 0x01) != 0) {
            locked = true;
            break;
        }
    }

    if (locked) {
        logger.debug("ADC clock synthesizer locked at 8 MHz");
    } else {
        logger.warning("ADC clock synthesizer did not lock during initialization");
    }
}

bool TunerHelper::changed_adc_clock(const uint8_t adc_clock_type) const {
    switch (adc_clock_type) {
        case ADC_CLOCK_FREQ_8_MHz:
        case ADC_CLOCK_FREQ_8_192_MHz:
        case ADC_CLOCK_FREQ_9_MHz:
        case ADC_CLOCK_FREQ_9_6_MHz:
            break;

        default:
            return false;
    }

    constexpr uint8_t REGE8 = 0x46 & 0xC0;
    constexpr uint8_t REGE9 = 0x54 & 0xF0;
    constexpr uint8_t REGEA = 0x07 & 0xC0;
    constexpr uint8_t REGEB = 0x27 & 0xC0;
    constexpr uint8_t REGEC = 0x1E & 0xC0;
    constexpr uint8_t REGED = 0x18 & 0x00;
    constexpr uint8_t REGEE = 0xB8 & 0x00;

    switch_page(REGISTER_PAGE_RF);

    set_register(0xE8, REGE8 | ADC_CLOCK_CONFIG[adc_clock_type][0]);
    set_register(0xE9, REGE9 | ADC_CLOCK_CONFIG[adc_clock_type][1]);
    set_register(0xEA, REGEA | ADC_CLOCK_CONFIG[adc_clock_type][2]);
    set_register(0xEB, REGEB | ADC_CLOCK_CONFIG[adc_clock_type][3]);
    set_register(0xEC, REGEC | ADC_CLOCK_CONFIG[adc_clock_type][4]);
    set_register(0xED, REGED | ADC_CLOCK_CONFIG[adc_clock_type][5]);
    set_register(0xEE, REGEE | ADC_CLOCK_CONFIG[adc_clock_type][6]);

    for (int i = 0; i < 10; i++) {
        if ((read_register(0x15) & 0x01) == 0) {
            return false;
        }
    }

    switch (adc_clock_type) {
        case ADC_CLOCK_FREQ_8_MHz: {
            switch_page(REGISTER_PAGE_COMM);
            set_register(0x6A, 0x01);

            switch_page(REGISTER_PAGE_FM);
            set_register(0x3C, 0x4B);
            set_register(0x3D, 0x37);
            set_register(0x3E, 0x89);
            set_register(0x3F, 0x41);
            set_register(0x54, 0x58);
            set_register(0x40, 0x8F);
            set_register(0x41, 0xC2);
            set_register(0x42, 0xF5);
            set_register(0x43, 0x00);
            break;
        }

        case ADC_CLOCK_FREQ_8_192_MHz: {
            switch_page(REGISTER_PAGE_COMM);
            set_register(0x6A, 0x01);

            switch_page(REGISTER_PAGE_FM);
            set_register(0x3C, 0x00);
            set_register(0x3D, 0x00);
            set_register(0x3E, 0x00);
            set_register(0x3F, 0x40);
            set_register(0x54, 0x58);
            set_register(0x40, 0x00);
            set_register(0x41, 0x00);
            set_register(0x42, 0xF0);
            set_register(0x43, 0x00);
            break;
        }

        case ADC_CLOCK_FREQ_9_MHz: {
            switch_page(REGISTER_PAGE_COMM);
            set_register(0x6A, 0x01);

            switch_page(REGISTER_PAGE_FM);
            set_register(0x3C, 0xB5);
            set_register(0x3D, 0x14);
            set_register(0x3E, 0x41);
            set_register(0x3F, 0x3A);
            set_register(0x54, 0x58);
            set_register(0x40, 0x0D);
            set_register(0x41, 0x74);
            set_register(0x42, 0xDA);
            set_register(0x43, 0x00);
            break;
        }

        case ADC_CLOCK_FREQ_9_6_MHz: {
            switch_page(REGISTER_PAGE_COMM);
            set_register(0x6A, 0x31);

            switch_page(REGISTER_PAGE_FM);
            set_register(0x3C, 0x69);
            set_register(0x3D, 0x03);
            set_register(0x3E, 0x9D);
            set_register(0x3F, 0x36);
            set_register(0x54, 0x58);
            set_register(0x40, 0xCC);
            set_register(0x41, 0xCC);
            set_register(0x42, 0xCC);
            set_register(0x43, 0x00);
            break;
        }

        default: {
            return false;
        }
    }

    return true;
}

void TunerHelper::tdmb_init_top() const {
    switch_page(REGISTER_PAGE_FM);

    set_register(0x07, 0x08);
    set_register(0x05, 0x17);
    set_register(0x06, 0x10);
    set_register(0x0A, 0x00);
}

void TunerHelper::tdmb_init_comm() const {
    switch_page(REGISTER_PAGE_COMM);

    set_register(0x10, 0x91);
    set_register(0xE1, 0x00);

    set_register(0x35, 0x7B);
    set_register(0x3B, 0x3C);

    set_register(0x36, 0x68);
    set_register(0x3A, 0x44);

    set_register(0x3C, 0x20);
    set_register(0x3D, 0x0B);
    set_register(0x3D, 0x09);

    set_register(0xA6, 0x30); //0x30 ==>NO TSOUT@Error packet, 0x10 ==> NULL PID PACKET@Error packet

    set_register(0xAA, 0x01); //Enable 0x47 insertion to video frame.

    set_register(0xAF, 0x07); //FEC
}

void TunerHelper::tdmb_init_host() const {
    switch_page(REGISTER_PAGE_HOST);

    set_register(0x10, 0x00);
    set_register(0x13, 0x16);
    set_register(0x14, 0x00);
    set_register(0x19, 0x0A);
    set_register(0xF0, 0x00);
    set_register(0xF1, 0x00);
    set_register(0xF2, 0x00);
    set_register(0xF3, 0x00);
    set_register(0xF4, 0x00);
    set_register(0xF5, 0x00);
    set_register(0xF6, 0x00);
    set_register(0xF7, 0x00);
    set_register(0xF8, 0x00);
    set_register(0xFB, 0xFF);
}

void TunerHelper::tdmb_init_of_dm() const {
    constexpr uint8_t INV_MODE = 0x1;
    constexpr uint8_t PWM_COM = 0x08;
    constexpr uint8_t WAGC_COM = 0x03;
    constexpr uint8_t AGC_MODE = 0x06;
    constexpr uint8_t POST_INIT = 0x09;
    constexpr uint8_t AGC_CYCLE = 0x10;

    switch_page(REGISTER_PAGE_FM);

    set_register(0x12, 0x04);
    set_register(0x13, 0x72);
    set_register(0x14, 0x63);
    set_register(0x15, 0x64);
    set_register(0x16, 0x6C);
    set_register(0x38, 0x01);

    set_register(0x1A, 0xB4);

    set_register(0x20, 0x5B);
    set_register(0x25, 0x09);

    set_register(0x44, (0x00 | POST_INIT));

    set_register(0x46, 0xA0);
    set_register(0x47, 0x0F);

    set_register(0x48, 0xB8);
    set_register(0x49, 0x0B);
    set_register(0x54, 0x58);

    set_register(0x55, 0x06);

    set_register(0x56, (0x00 | AGC_CYCLE));

    set_register(0x59, 0x51);

    set_register(0x5A, 0x1C);

    set_register(0x6D, 0x00);
    set_register(0x8B, 0x34);
    set_register(0x6A, 0x1C);

    set_register(0x6B, 0x2D);
    set_register(0x85, 0x32);

    set_register(0x8D, 0x0C);
    set_register(0x8E, 0x01);

    set_register(0x33, (0x00 | (INV_MODE << 1)));
    set_register(0x53, (0x00 | AGC_MODE));

    set_register(0x6F, (0x00 | WAGC_COM));

    set_register(0xBA, PWM_COM);

    switch_page(REGISTER_PAGE_COMM);

    set_register(0x6A, 0x01);

    switch_page(REGISTER_PAGE_FM);

    set_register(0x3C, 0x4B);
    set_register(0x3D, 0x37);
    set_register(0x3E, 0x89);
    set_register(0x3F, 0x41);


    set_register(0x40, 0x8F);

    set_register(0x41, 0xC2);

    set_register(0x42, 0xF5);
    set_register(0x43, 0x00);

    set_register(0x94, 0x08);

    set_register(0x98, 0x05);
    set_register(0x99, 0x03);
    set_register(0x9B, 0xCF);
    set_register(0x9C, 0x10);
    set_register(0x9D, 0x1C);
    set_register(0x9F, 0x32);
    set_register(0xA0, 0x90);

    set_register(0xA2, 0xA0);

    set_register(0xA3, 0x08);

    set_register(0xA4, 0x01);

    set_register(0xA8, 0xF6);
    set_register(0xA9, 0x89);
    set_register(0xAA, 0x0C);
    set_register(0xAB, 0x32);

    set_register(0xAC, 0x14);
    set_register(0xAD, 0x09);

    set_register(0xAE, 0xFF);

    set_register(0xEB, 0x6B);

    set_register(0x93, 0x10);
    set_register(0x94, 0x29);
    set_register(0xA2, 0x50);
    set_register(0xA4, 0x02);
    set_register(0xAF, 0x01);
}

void TunerHelper::tdmb_init_fec() const {
    switch_page(REGISTER_PAGE_DD);

    set_register(0x80, 0x80);
    set_register(0x81, 0xFF);
    set_register(0x87, 0x07);

    set_register(0x45, 0xA1);

    set_register(0xDD, 0xD0);
    set_register(0x39, 0x07);
    set_register(0xE6, 0x10);
    set_register(0xA5, 0xA0);
}

void TunerHelper::reset_memory_fic() const {
    switch_page(REGISTER_PAGE_DD);
    set_register(0x46, 0x00);
}

void TunerHelper::oem_configure_interrupt() const {
    switch_page(REGISTER_PAGE_DD);

    set_register(0x09, 0x00); /* [6]INT1 [5]INT0 - 1: Input mode, 0: Output mode */

    set_register(0x0B, 0x00); /* [2]INT1 PAD disable [1]INT0 PAD disable */

    switch_page(REGISTER_PAGE_HOST);

    set_register(0x28, 0x01); /* [5:3]INT1 out sel [2:0] INI0 out sel - 0:Toggle 1:Level,, 2:"0", 3:"1"*/

    set_register(0x2A, 0x13); /* [5]INT1 pol [4]INT0 pol - 0:Active High, 1:Active Low [3:0] Period = (INT_TIME+1)/8MHz*/
}

void TunerHelper::stream_disable() const {
    switch_page(REGISTER_PAGE_HOST);

    set_register(0x29, 0x08);
}

void TunerHelper::configure_host_if() const {
    switch_page(REGISTER_PAGE_HOST);

    set_register(0x77, 0x14); /*SPI Mode Enable*/
    set_register(0x04, 0x28); /*SPI Mode Enable*/
    set_register(0x0C, 0xF5);
}

void TunerHelper::rf_initialize() const {
    switch_page(REGISTER_PAGE_RF);

    set_register(0x27, 0x96);
    set_register(0x2B, 0x88);
    set_register(0x2D, 0xEC);
    set_register(0x2E, 0xB0);
    set_register(0x31, 0x04);
    set_register(0x34, 0xF8);
    set_register(0x35, 0x14);
    set_register(0x3A, 0x62);
    set_register(0x3B, 0x74);
    set_register(0x3C, 0xA8);
    set_register(0x43, 0x36);
    set_register(0x44, 0x70);
    set_register(0x46, 0x07);
    set_register(0x49, 0x1F);
    set_register(0x4A, 0x50);
    set_register(0x4B, 0x80);
    set_register(0x53, 0x20);
    set_register(0x55, 0xD6);
    set_register(0x5A, 0x83);
    set_register(0x60, 0x13);
    set_register(0x6B, 0x85);
    set_register(0x6E, 0x88);
    set_register(0x6F, 0x78);
    set_register(0x72, 0xF0);
    set_register(0x73, 0xCA);
    set_register(0x77, 0x80);
    set_register(0x78, 0x47);
    set_register(0x84, 0x80);
    set_register(0x85, 0x90);
    set_register(0x86, 0x80);
    set_register(0x87, 0x98);
    set_register(0x8A, 0xF6);
    set_register(0x8B, 0x80);
    set_register(0x8C, 0x75);
    set_register(0x8F, 0xF9);
    set_register(0x90, 0x91);
    set_register(0x93, 0x2D);
    set_register(0x94, 0x23);
    set_register(0x99, 0x77);
    set_register(0x9A, 0x2D);
    set_register(0x9B, 0x1E);
    set_register(0x9C, 0x47);
    set_register(0x9D, 0x3A);
    set_register(0x9E, 0x03);
    set_register(0x9F, 0x1E);
    set_register(0xA0, 0x22);
    set_register(0xA1, 0x33);
    set_register(0xA2, 0x51);
    set_register(0xA3, 0x36);
    set_register(0xA4, 0x0C);
    set_register(0xAE, 0x37);
    set_register(0xB5, 0x9B);
    set_register(0xBD, 0x45);
    set_register(0xBE, 0x68);
    set_register(0xBF, 0x5C);
    set_register(0xC0, 0x33);
    set_register(0xC1, 0xCA);
    set_register(0xC2, 0x43);
    set_register(0xC3, 0x90);
    set_register(0xC4, 0xEC);
    set_register(0xC5, 0x17);
    set_register(0xC6, 0xDA);
    set_register(0xC7, 0x41);
    set_register(0xC8, 0x49);
    set_register(0xC9, 0xBC);
    set_register(0xCA, 0x3A);
    set_register(0xCB, 0x20);
    set_register(0xCC, 0x43);
    set_register(0xCD, 0xA6);
    set_register(0xCE, 0x4C);
    set_register(0xCF, 0x1F);
    set_register(0xD0, 0x8D);
    set_register(0xD1, 0x3D);
    set_register(0xD2, 0xDF);
    set_register(0xD3, 0xA4);
    set_register(0xD4, 0x30);
    set_register(0xD5, 0x00);
    set_register(0xD6, 0x41);
    set_register(0xD7, 0x81);
    set_register(0xD8, 0xD0);
    set_register(0xD9, 0x00);
    set_register(0xDA, 0x00);
    set_register(0xDB, 0x41);
    set_register(0xDC, 0x6D);
    set_register(0xDD, 0xAC);
    set_register(0xDE, 0x39);
    set_register(0xDF, 0x4E);
    set_register(0xE0, 0x43);
    set_register(0xE1, 0xA0);
    set_register(0xE2, 0x4C);
    set_register(0xE3, 0x1D);
    set_register(0xE4, 0x99);
    set_register(0xE5, 0x3B);
    set_register(0xE6, 0x00);
    set_register(0xE7, 0x0D);
    set_register(0xA5, 0x00);
    set_register(0xE9, 0x41);
    set_register(0xAE, 0x77);
    set_register(0xE9, 0x51);

    /* Auto LNA from above definition */
    set_register(0x37, 0x3B);
    set_register(0x39, 0x2C);

    const short check_val = static_cast<short>((read_register(0x10) << 8 | read_register(0x11)) & 0xFFFF);
    logger.debug(std::format("RF check value: 0x{:04X}", static_cast<uint16_t>(check_val)));

    if (check_val != static_cast<short>(0xFFFF)) {
        set_register(0x2C, 0x48);
        set_register(0x47, 0xE0);
        set_register(0x35, 0x04);

        set_register(0x2A, 0x05);
        set_register(0x2D, 0x8C);
        set_register(0x61, 0x25);
    } else {
        set_register(0x2C, 0xC8);
        set_register(0x47, 0xB0);
        set_register(0x35, 0x04);

        set_register(0x2A, 0x07);
        set_register(0x2D, 0xEC);
        set_register(0x61, 0x2A);
    }

    switch_page(REGISTER_PAGE_RF);
    set_register(0x6B, 0xC5);
}

void TunerHelper::rf_special() const {
    std::vector<uint8_t> read_req = {0x10, 0x00, 0x00, 0x01};
    bulk_transfer(ENDPOINT_OUT, read_req);

    std::vector<uint8_t> read_req_buffer(4);
    bulk_transfer(ENDPOINT_IN, read_req_buffer);

    read_req = {0x00, 0x00, 0x00, 0x00};
    bulk_transfer(ENDPOINT_OUT, read_req);
    bulk_transfer(ENDPOINT_IN, read_req_buffer);

    read_req = {0x01, 0x00, 0x00, 0x00};
    bulk_transfer(ENDPOINT_OUT, read_req);
    bulk_transfer(ENDPOINT_IN, read_req_buffer);
}

void TunerHelper::setup_msc_threshold() const {
    switch_page(REGISTER_PAGE_DD);

    set_register(0x56, 0x08);
    set_register(0x57, 0x00);

    switch_page(REGISTER_PAGE_DD);
    logger.debug("MSC threshold configured: 2048 bytes");
}

void TunerHelper::setup_memory_fic() const {
    switch_page(REGISTER_PAGE_DD);

    set_register(0x46, 0x14);
    set_register(0x46, 0x16);
    logger.debug("FIC memory configured");
}

bool TunerHelper::set_frequency(const uint32_t frequency) {
    const auto operation = begin_operation(TUNING_TIMEOUT);
    const auto channel_index = get_dab_channel_index(frequency);

    if (!channel_index.has_value()) {
        logger.warning(std::format("Frequency {} kHz is not a supported Band III channel", frequency));
        return false;
    }

    const size_t index = *channel_index;
    const uint8_t adc_clock_type = ADC_CLOCK_ENSEMBLE_MAP[index];

    if (!changed_adc_clock(adc_clock_type)) {
        logger.warning(std::format("Could not configure ADC clock type {}", adc_clock_type));
        return false;
    }

    switch_page(REGISTER_PAGE_RF);

    const uint32_t pll_nf = PLL_NF_ENSEMBLE_MAP[index];

    set_register(0x23, static_cast<uint8_t>(pll_nf >> 22 & 0xFF));
    set_register(0x24, static_cast<uint8_t>(pll_nf >> 14 & 0xFF));
    set_register(0x25, static_cast<uint8_t>(pll_nf >> 6 & 0xFF));
    set_register(0x26, static_cast<uint8_t>((pll_nf & 0x3F) << 2 | 0x01));

    const auto pll_start = std::chrono::steady_clock::now();
    const auto pll_deadline = pll_start + std::chrono::milliseconds(PLL_TIMEOUT_MS);
    std::this_thread::sleep_for(std::chrono::milliseconds(PLL_MIN_SETTLE_MS));

    bool pll_locked = (read_register(0x15) & 0x02) != 0;

    while (!pll_locked && operation.succeeded() && std::chrono::steady_clock::now() < pll_deadline) {
        std::this_thread::sleep_until(std::min(pll_deadline, std::chrono::steady_clock::now() + std::chrono::milliseconds(PLL_POLL_MS)));
        pll_locked = (read_register(0x15) & 0x02) != 0;
    }

    set_register(0x20, 0x00);

    if (!pll_locked || !operation.succeeded()) {
        logger.warning(std::format("RF PLL did not lock within {} ms", PLL_TIMEOUT_MS));
        return false;
    }

    logger.debug(std::format("RF PLL locked after {} ms", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - pll_start).count()));

    switch_page(REGISTER_PAGE_FM);
    set_register(0x10, 0x48);
    set_register(0x10, 0xC9);

    current_subchannel = 0xFF;
    close_background_subchannel();
    logger.debug(std::format("RF tuned to {} kHz", frequency));
    return operation.succeeded();
}

void TunerHelper::open_subchannel(const uint8_t subchannel) {
    const auto operation = begin_operation(RECEIVE_TIMEOUT);
    if (current_subchannel == subchannel) {
        return;
    }

    switch_page(REGISTER_PAGE_DD);
    set_register(INT_E_UCLRL, 0x70);

    switch_page(REGISTER_PAGE_HOST);
    set_register(0x62, 0x00);

    switch_page(REGISTER_PAGE_DD);
    set_register(0x3A, static_cast<uint8_t>(0x80 | subchannel));
    clear_and_setup_msc_memory();

    if (!operation.succeeded()) {
        current_subchannel = 0xFF;
        return;
    }

    current_subchannel = subchannel;
    logger.debug(std::format("MSC subchannel set to {}", subchannel));
}

void TunerHelper::open_background_subchannel(const uint8_t subchannel) {
    const auto operation = begin_operation(RECEIVE_TIMEOUT);
    if (background_subchannel == subchannel) {
        return;
    }

    switch_page(REGISTER_PAGE_DD);
    set_register(0x47, 0x00);
    set_register(0x31, static_cast<uint8_t>(read_register(0x31) & ~0x03));
    set_register(0x3D, static_cast<uint8_t>(0x80 | subchannel));
    set_register(0x50, 0x02);
    set_register(0x51, 0x00);
    set_register(INT_E_UCLRL, 0x02);
    clear_and_setup_msc0_memory();

    if (!operation.succeeded()) {
        background_subchannel = 0xFF;
        return;
    }

    background_subchannel = subchannel;
    logger.debug(std::format("Background MSC subchannel set to {}", subchannel));
}

void TunerHelper::close_background_subchannel() {
    const auto operation = begin_operation(RECEIVE_TIMEOUT);
    if (background_subchannel == 0xFF) {
        return;
    }

    switch_page(REGISTER_PAGE_DD);
    set_register(0x3D, 0x00);
    set_register(0x47, 0x00);
    set_register(INT_E_UCLRL, 0x02);
    background_subchannel = 0xFF;
}

void TunerHelper::read_data() const {
    const auto operation = begin_operation(RECEIVE_TIMEOUT);
    switch_page(REGISTER_PAGE_DD);

    const uint8_t interrupt_status = read_register(INT_E_STATL);
    const bool msc1_overrun = (interrupt_status & MSC1_E_OVER_FLOW) != 0;
    const bool msc1_underrun = (interrupt_status & MSC1_E_UNDER_FLOW) != 0;
    const bool msc1_interrupt = (interrupt_status & MSC1_E_INT) != 0;
    const bool fic_interrupt = (interrupt_status & FIC_E_INT) != 0;
    const bool msc0_overrun = (interrupt_status & MSC0_E_OVER_FLOW) != 0;
    const bool msc0_underrun = (interrupt_status & MSC0_E_UNDER_FLOW) != 0;
    const bool msc0_interrupt = (interrupt_status & MSC0_E_INT) != 0;

    if (fic_interrupt) {
        switch_page(REGISTER_PAGE_FIC);

        std::vector<uint8_t> request{0x22, 0x00, 0x80, 0x01, 0x10};
        bulk_transfer(ENDPOINT_OUT, request);

        std::vector<uint8_t> response(400);
        const int transferred = bulk_transfer(ENDPOINT_IN, response);

        switch_page(REGISTER_PAGE_DD);
        set_register(INT_E_UCLRL, 0x01);

        if (fic_callback && transferred >= 4) {
            fic_callback(std::vector(response.begin() + 4, response.begin() + transferred));
        }
    }

    if (msc1_overrun || msc1_underrun) {
        if (msc1_overrun) {
            logger.warning("MSC1 memory overrun");
        }

        if (msc1_underrun) {
            logger.warning("MSC1 memory underrun");
        }

        clear_and_setup_msc_memory();
    } else if (msc1_interrupt) {
        switch_page(REGISTER_PAGE_MSC1);

        std::vector<uint8_t> request{0x22, 0x00, 0x00, 0x08, 0x10};
        bulk_transfer(ENDPOINT_OUT, request);

        std::vector<uint8_t> response(4096);
        const int transferred = bulk_transfer(ENDPOINT_IN, response);

        if (msc_callback && transferred >= 4) {
            msc_callback(std::vector(response.begin() + 4, response.begin() + transferred));
        }

        switch_page(REGISTER_PAGE_DD);
        set_register(INT_E_UCLRL, 0x04);
    }

    if (background_subchannel != 0xFF) {
        if (msc0_overrun || msc0_underrun) {
            logger.warning(msc0_overrun ? "MSC0 memory overrun" : "MSC0 memory underrun");
            clear_and_setup_msc0_memory();
            set_register(INT_E_UCLRL, 0x02);
        } else if (msc0_interrupt) {
            switch_page(REGISTER_PAGE_MSC0);

            std::vector<uint8_t> request{0x22, 0x00, 0x00, 0x02, 0x10};
            bulk_transfer(ENDPOINT_OUT, request);

            std::vector<uint8_t> response(1024);
            const int transferred = bulk_transfer(ENDPOINT_IN, response);

            if (background_msc_callback && transferred >= 4) {
                background_msc_callback(std::vector(response.begin() + 4, response.begin() + transferred));
            }

            switch_page(REGISTER_PAGE_DD);
            set_register(INT_E_UCLRL, 0x02);
        }
    }
}

int TunerHelper::take_usb_error() const {
    return pending_usb_error.exchange(LIBUSB_SUCCESS);
}

TunerHelper::SignalStatus TunerHelper::get_signal_status() const {
    const auto operation = begin_operation(RECEIVE_TIMEOUT);
    SignalStatus status;

    switch_page(REGISTER_PAGE_DD);

    status.ofdm_lock = (read_register(0x37) & 0x01) != 0;
    status.fec_lock = (read_register(0xFB) & 0x03) == 0x03;
    status.fec_sync = ((read_register(0xD7) >> 4) & 0x01) != 0;

    if (!status.ofdm_lock) {
        return status;
    }

    const uint32_t period = static_cast<uint32_t>(read_register(0x88)) << 24
        | static_cast<uint32_t>(read_register(0x89)) << 16
        | static_cast<uint32_t>(read_register(0x8A)) << 8
        | read_register(0x8B);
    const uint32_t errors = static_cast<uint32_t>(read_register(0x8C)) << 24
        | static_cast<uint32_t>(read_register(0x8D)) << 16
        | static_cast<uint32_t>(read_register(0x8E)) << 8
        | read_register(0x8F);

    if (period == 0 || !status.fec_sync) {
        return status;
    }

    status.msc_cer_valid = true;
    status.msc_cer = static_cast<uint32_t>(std::min<uint64_t>(10000, static_cast<uint64_t>(errors) * 10000 / period));

    uint32_t antenna_cer = errors <= 4000 ? 0 : status.msc_cer;

    if (antenna_cer > 1200) {
        antenna_cer = 2000;
    }

    while (static_cast<size_t>(status.antenna_level) + 1 < DAB_ANTENNA_LEVEL_CER_THRESHOLDS.size() && antenna_cer < DAB_ANTENNA_LEVEL_CER_THRESHOLDS[status.antenna_level]) {
        status.antenna_level++;
    }

    return status;
}

void TunerHelper::clear_and_setup_msc_memory() const {
    switch_page(REGISTER_PAGE_DD);
    set_register(0x48, 0x00);
    set_register(0x48, 0x05);
}

void TunerHelper::clear_and_setup_msc0_memory() const {
    switch_page(REGISTER_PAGE_DD);
    set_register(0x47, 0x00);
    set_register(0x47, 0x05);
}
