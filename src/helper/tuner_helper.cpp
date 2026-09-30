//
// Created by tarek on 9/30/26.
//

#include "tuner_helper.h"
#include <iostream>
#include <vector>
#include <bits/this_thread_sleep.h>

TunerHelper::TunerHelper(libusb_device_handle *device_handle) {
    this->device_handle = device_handle;
}

int TunerHelper::bulk_transfer(const uint8_t endpoint, std::vector<uint8_t> &buffer) const {
    int transferred;
    libusb_bulk_transfer(device_handle, endpoint, buffer.data(), buffer.size(), &transferred, 5000);
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
    bulk_transfer(ENDPOINT_OUT, data);

    std::vector<uint8_t> read_buffer(5);
    bulk_transfer(ENDPOINT_IN, read_buffer);

    return read_buffer[4];
}

bool TunerHelper::power_up() const {
    //trying some time to power the chip up

    for (int i = 0; i < 100; i++) {
        switch_page(REGISTER_PAGE_HOST);
        set_register(0x7D, 0x06);

        if (read_register(0x7D) == 0x06) {
            std::cout << "Tuner Handler: Power-Up successful" << std::endl;

            configure_power_type();
            configure_adc_clock();
            tdmb_init_top();
            tdmb_init_comm();
            tdmb_init_host();
            tdmb_init_of_dm();
            tdmb_init_fec();
            reset_memory_fic();
            oem_configure_interrupt();
            stream_disable();
            configure_host_if();
            rf_initialize();
            rf_special();
            setup_msc_threshold();
            setup_memory_fic();

            return true;
        }
    }

    std::cout << "Tuner Handler: Power-Up failed" << std::endl;
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

    for (int i = 0; i < 10; i++) {
        uint8_t RD15 = read_register(0x15);
        RD15 = static_cast<uint8_t>(RD15 & 0x01);

        if (RD15 > 0) {
            break;
        }

        std::cout << "Tuner Handler: ADCClock SYNTH 1st step  UnLock..." << std::endl;
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

    switch_page(REGISTER_PAGE_FM); //instead of OFDM page

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
    switch_page(REGISTER_PAGE_DD); //instead of FEC_PAGE

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
    std::cout << "Tuner Handler: Setting up MSC threshold..." << std::endl;

    switch_page(REGISTER_PAGE_DD);

    //1536
    //setRegister(0x56, 0x00);
    //setRegister(0x57, 0xD8);

    //Threshold at 1024
    //setRegister(0x56, 0x04);
    //setRegister(0x57, 0x00);

    //Threshold at 2048
    set_register(0x56, 0x08);
    set_register(0x57, 0x00);

    switch_page(REGISTER_PAGE_DD);
}

void TunerHelper::setup_memory_fic() const {
    std::cout << "Tuner Handler: Setting up FIC memory..." << std::endl;

    switch_page(REGISTER_PAGE_DD);

    /* auto user clr, get fic  CRC 2byte including[4] */
    set_register(0x46, 0x14);

    set_register(0x46, 0x16);
}

bool TunerHelper::set_frequency(const uint32_t frequency) const {
    if (frequency < FREQ_START) {
        return false;
    }

    int index = static_cast<int>((frequency - FREQ_START) / FREQ_STEP);

    if (frequency >= 224096) {
        index += 3;
    } else if (frequency >= 217008) {
        index += 2;
    } else if (frequency >= 210096) {
        index += 1;
    }

    constexpr size_t table_size = std::size(ADC_CLOCK_ENSEMBLE_MAP);

    if (index < 0 || static_cast<size_t>(index) >= table_size) {
        return false;
    }

    const uint8_t adc_clock_type = ADC_CLOCK_ENSEMBLE_MAP[index];

    if (!changed_adc_clock(adc_clock_type)) {
        return false;
    }

    switch_page(REGISTER_PAGE_RF);

    const uint32_t pll_nf = PLL_NF_ENSEMBLE_MAP[index];

    set_register(0x23, static_cast<uint8_t>(pll_nf >> 22 & 0xFF));
    set_register(0x24, static_cast<uint8_t>(pll_nf >> 14 & 0xFF));
    set_register(0x25, static_cast<uint8_t>(pll_nf >> 5 & 0xFF));
    set_register(0x26, static_cast<uint8_t>((pll_nf & 0x3F) << 2 | 0x01));

    std::this_thread::sleep_for(std::chrono::milliseconds(PLL_MIN_SETTLE_MS));

    unsigned waited_ms = PLL_MIN_SETTLE_MS;
    bool pll_locked = (read_register(0x15) & 0x02) != 0;

    while (!pll_locked && waited_ms < PLL_TIMEOUT_MS) {
        const unsigned wait_ms = std::min(PLL_POLL_MS, PLL_TIMEOUT_MS - waited_ms);
        std::this_thread::sleep_for(std::chrono::milliseconds(wait_ms));
        waited_ms += wait_ms;
        pll_locked = (read_register(0x15) & 0x02) != 0;
    }

    set_register(0x20, 0x00);

    if (!pll_locked) {
        return false;
    }

    switch_page(REGISTER_PAGE_FM);
    set_register(0x10, 0x48);
    set_register(0x10, 0xC9);

    return true;
}
