//
// Created by marko on 11.11.2020..
//

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include "CC1101.h"
#include "driver/spi.h"

static const char *log = "cc1101";

void cc1101_spiInitialize();

void cc1101_waitMisoLow();

void cc1101_sendCommand(uint8_t cmd);

uint8_t cc1101_readRegister(uint8_t address, enum ReadRegisterType readType);

esp_err_t cc1101_waitUntilState(uint8_t waitForState) {
    for (uint8_t i = 0; i < 100; i++) {
        int state = cc1101_readRegister(CC1101_MARCSTATE, Burst) & 0x1f;
        if (state == waitForState) break;
        vTaskDelay(5 / portTICK_PERIOD_MS);
        if(i == 99) {
            ESP_LOGE(log, "Could not set state of transmitter - state is %02x\n", state);
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

void cc1101_writeRegister(uint8_t address, uint8_t value);

void cc1101_initialize() {
    cc1101_resetChip();
    //
    // Rf settings for CC1101
    //
    cc1101_writeRegister(CC1101_IOCFG2, 0x0B);   //GDO2 Output Pin Configuration
    cc1101_writeRegister(CC1101_IOCFG0, 0x0C);   //GDO0 Output Pin Configuration
    cc1101_writeRegister(CC1101_FIFOTHR, 0x47);  //RX FIFO and TX FIFO Thresholds
    cc1101_writeRegister(CC1101_PKTCTRL0, 0x12); //Packet Automation Control
    cc1101_writeRegister(CC1101_FSCTRL1, 0x06);  //Frequency Synthesizer Control
    cc1101_writeRegister(CC1101_FREQ2, 0x10);    //Frequency Control Word, High Byte
    cc1101_writeRegister(CC1101_FREQ1, 0xB0);    //Frequency Control Word, Middle Byte
    cc1101_writeRegister(CC1101_FREQ0, 0xA3);    //Frequency Control Word, Low Byte
    cc1101_writeRegister(CC1101_MDMCFG4, 0x8D);  //Modem Configuration
    cc1101_writeRegister(CC1101_MDMCFG3, 0x3B);  //Modem Configuration
    cc1101_writeRegister(CC1101_MDMCFG2, 0x30);  //Modem Configuration
    cc1101_writeRegister(CC1101_MDMCFG1, 0x00);  //Modem Configuration
    cc1101_writeRegister(CC1101_DEVIATN, 0x15);  //Modem Deviation Setting
    cc1101_writeRegister(CC1101_MCSM0, 0x18);    //Main Radio Control State Machine Configuration
    cc1101_writeRegister(CC1101_FOCCFG, 0x16);   //Frequency Offset Compensation Configuration
    cc1101_writeRegister(CC1101_WORCTRL, 0xFB);  //Wake On Radio Control
    cc1101_writeRegister(CC1101_FREND0, 0x11);   //Front End TX Configuration
    cc1101_writeRegister(CC1101_FSCAL3, 0xEA);   //Frequency Synthesizer Calibration
    cc1101_writeRegister(CC1101_FSCAL2, 0x2A);   //Frequency Synthesizer Calibration
    cc1101_writeRegister(CC1101_FSCAL1, 0x00);   //Frequency Synthesizer Calibration
    cc1101_writeRegister(CC1101_FSCAL0, 0x1F);   //Frequency Synthesizer Calibration
    cc1101_writeRegister(CC1101_TEST2, 0x81);    //Various Test Settings
    cc1101_writeRegister(CC1101_TEST1, 0x35);    //Various Test Settings
    cc1101_writeRegister(CC1101_TEST0, 0x09);    //Various Test Settings

//    cc1101_sendCommand(CC1101_SIDLE);
//    cc1101_waitUntilState(0x01);
//
//    cc1101_calibrate();
}

void cc1101_resetChip() {
    spi_deinit(HSPI_HOST);
    gpio_config_t input;
    input.mode = GPIO_MODE_OUTPUT;
    input.pull_up_en = GPIO_PULLUP_ENABLE;
    input.pull_down_en = GPIO_PULLDOWN_DISABLE;
    input.intr_type = GPIO_INTR_DISABLE;
    input.pin_bit_mask = 1 << CC1101_CS | 1 << CC1101_MOSI | 1 << CC1101_CLK;
    gpio_config(&input);

//    gpio_config_t output;
//    output.mode = GPIO_MODE_OUTPUT;
//    output.pull_up_en = GPIO_PULLUP_DISABLE;
//    output.pull_down_en = GPIO_PULLDOWN_ENABLE;
//    output.intr_type = GPIO_INTR_DISABLE;
//    output.pin_bit_mask = 1<<CC1101_MISO;
//    gpio_config(&output);

    gpio_set_level(CC1101_CLK, 1);
    gpio_set_level(CC1101_MOSI, 0);
    gpio_set_level(CC1101_CS, 0);
    gpio_set_level(CC1101_CS, 1);
    gpio_set_level(CC1101_CS, 0);
    gpio_set_level(CC1101_CS, 1);
    vTaskDelay(1 / portTICK_PERIOD_MS);
    gpio_set_level(CC1101_CS, 0);
    cc1101_waitMisoLow();
    cc1101_spiInitialize();
    cc1101_sendCommand(CC1101_SRES);
}

esp_err_t cc1101_calibrate() {
    cc1101_sendCommand(CC1101_SCAL);
    return cc1101_waitUntilState(0x01); // idle state
}

esp_err_t cc1101_startTX() {
    cc1101_sendCommand(CC1101_STX);
    return cc1101_waitUntilState(0x13); // tx state
}

void cc1101_stop() {
    cc1101_sendCommand(CC1101_SIDLE);
}

void cc1101_spiInitialize() {
    spi_config_t spiConfig;
    spiConfig.interface.val = SPI_DEFAULT_INTERFACE;
    spiConfig.intr_enable.val = SPI_MASTER_DEFAULT_INTR_ENABLE;
    spiConfig.interface.cs_en = 0;
//    spiConfig.interface.cpol = 1;
//    spiConfig.interface.cpha = 1;
    spiConfig.clk_div = SPI_2MHz_DIV;
    spiConfig.mode = SPI_MASTER_MODE;
    spiConfig.event_cb = NULL;
    spi_init(HSPI_HOST, &spiConfig);

    gpio_config_t cfg;
    cfg.pin_bit_mask = 1UL << CC1101_CS;
    cfg.intr_type = GPIO_INTR_DISABLE;
    cfg.mode = GPIO_MODE_OUTPUT;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&cfg);
}

void cc1101_waitMisoLow() {
    while (gpio_get_level(CC1101_MISO) != 0);
}

void cc1101_sendCommand(uint8_t cmd) {
    gpio_set_level(CC1101_CS, 0);
    cc1101_waitMisoLow();

    uint32_t in = cmd;
    spi_trans_t frame;
    frame.addr = NULL;
    frame.cmd = NULL;
    frame.mosi = &in;
    frame.bits.cmd = 0;
    frame.bits.addr = 0;
    frame.bits.miso = 0;
    frame.bits.mosi = 8;
    spi_trans(HSPI_HOST, &frame);

    gpio_set_level(CC1101_CS, 1);
}

uint8_t cc1101_readRegister(uint8_t address, enum ReadRegisterType readType) {
    uint8_t addr = address | readType;
    gpio_set_level(CC1101_CS, 0);
    cc1101_waitMisoLow();

    uint32_t in = addr;
    uint32_t out = 0;
    spi_trans_t frame;
    frame.addr = NULL;
    frame.cmd = NULL;
    frame.mosi = &in;
    frame.miso = &out;
    frame.bits.cmd = 0;
    frame.bits.addr = 0;
    frame.bits.miso = 8;
    frame.bits.mosi = 8;
    spi_trans(HSPI_HOST, &frame);

    // read result
    gpio_set_level(CC1101_CS, 1);
    return out;
}

void cc1101_writeRegister(uint8_t address, uint8_t value) {
    gpio_set_level(CC1101_CS, 0);
    cc1101_waitMisoLow();

    uint32_t in = value << 8 | address;
    spi_trans_t frame;
    frame.addr = NULL;
    frame.cmd = NULL;
    frame.mosi = &in;
    frame.miso = NULL;
    frame.bits.cmd = 0;
    frame.bits.addr = 0;
    frame.bits.mosi = 16;
    frame.bits.miso = 0;
    spi_trans(HSPI_HOST, &frame);

    gpio_set_level(CC1101_CS, 1);
}