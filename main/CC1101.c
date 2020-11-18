//
// Created by marko on 11.11.2020..
//

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <driver/hw_timer.h>
#include <freertos/semphr.h>
#include "CC1101.h"
#include "driver/spi.h"

static const char *TAG = "cc1101";

void cc1101_spiInitialize();

void cc1101_waitMisoLow();

void cc1101_sendCommand(uint8_t cmd);

uint8_t cc1101_readRegister(uint8_t address, ReadRegisterType readType);

void cc1101_writeRegister(uint8_t address, uint8_t value);

esp_err_t cc1101_waitUntilState(uint8_t waitForState) {
    for (uint8_t i = 0; i < 100; i++) {
        int state = cc1101_readRegister(CC1101_MARCSTATE, Burst) & 0x1f;
        if (state == waitForState) break;
        vTaskDelay(5 / portTICK_PERIOD_MS);
        if (i == 99) {
            ESP_LOGE(TAG, "Could not set state of transmitter - state is %02x\n", state);
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

esp_err_t cc1101_initialize() {
    cc1101_spiInitialize();
    cc1101_por_sres();
    return cc1101_writeInitialRegisters();
}

esp_err_t cc1101_writeInitialRegisters() {
    //
    // Rf settings for CC1101
    //
    uint8_t registryData[24][2] = {
            {CC1101_IOCFG2,   0x0B},    //GDO2 Output Pin Configuration
            {CC1101_IOCFG0,   0x0C},    //GDO0 Output Pin Configuration
            {CC1101_FIFOTHR,  0x47},    //RX FIFO and TX FIFO Thresholds
            {CC1101_PKTCTRL0, 0x12},    //Packet Automation Control
            {CC1101_FSCTRL1,  0x06},    //Frequency Synthesizer Control
            {CC1101_FREQ2,    0x10},    //Frequency Control Word, High Byte
            {CC1101_FREQ1,    0xB0},    //Frequency Control Word, Middle Byte
            {CC1101_FREQ0,    0xA3},    //Frequency Control Word, Low Byte
            {CC1101_MDMCFG4,  0x8D},    //Modem Configuration
            {CC1101_MDMCFG3,  0x3B},    //Modem Configuration
            {CC1101_MDMCFG2,  0x30},    //Modem Configuration
            {CC1101_MDMCFG1,  0x00},    //Modem Configuration
            {CC1101_DEVIATN,  0x15},    //Modem Deviation Setting
            {CC1101_MCSM0,    0x18},    //Main Radio Control State Machine Configuration
            {CC1101_FOCCFG,   0x16},    //Frequency Offset Compensation Configuration
            {CC1101_WORCTRL,  0xFB},    //Wake On Radio Control
            {CC1101_FREND0,   0x11},    //Front End TX Configuration
            {CC1101_FSCAL3,   0xEA},    //Frequency Synthesizer Calibration
            {CC1101_FSCAL2,   0x2A},    //Frequency Synthesizer Calibration
            {CC1101_FSCAL1,   0x00},    //Frequency Synthesizer Calibration
            {CC1101_FSCAL0,   0x1F},    //Frequency Synthesizer Calibration
            {CC1101_TEST2,    0x81},    //Various Test Settings
            {CC1101_TEST1,    0x35},    //Various Test Settings
            {CC1101_TEST0,    0x09},    //Various Test Settings
    };
    for(uint8_t i = 0; i < 24; i++) {
        uint8_t address = registryData[i][0];
        uint8_t value = registryData[i][1];
        cc1101_writeRegister(address, value);
        uint8_t realRegisterValue = cc1101_readRegister(address, Single);
        if(realRegisterValue != value) {
            ESP_LOGE(TAG, "Registry write %02x, not successful, is %02x, should be %02x", address, realRegisterValue, value);
            return ESP_FAIL;
        }
    }

    return ESP_OK;
}

SemaphoreHandle_t sem = NULL;

void timer_callback() {
    xSemaphoreGive(sem);
}

void delay_us(uint32_t us) {
    if (sem == NULL) sem = xSemaphoreCreateBinary();
    hw_timer_init(timer_callback, NULL);
    hw_timer_alarm_us(us, false);
    xSemaphoreTake(sem, portTICK_PERIOD_MS);
    hw_timer_deinit();
}

void cc1101_SRES() {
    gpio_set_level(CC1101_CS, 0);
    uint32_t in = CC1101_SRES;
    spi_trans_t frame;
    frame.addr = NULL;
    frame.cmd = NULL;
    frame.mosi = &in;
    frame.bits.cmd = 0;
    frame.bits.addr = 0;
    frame.bits.miso = 0;
    frame.bits.mosi = 8;
    spi_trans(HSPI_HOST, &frame);

    cc1101_waitMisoLow();
    gpio_set_level(CC1101_CS, 1);
}

void cc1101_por_sres() {
    ESP_LOGI(TAG, "Starting CC1101 reset");
    gpio_set_level(CC1101_CS, 0);
    gpio_set_level(CC1101_CS, 1);
    gpio_set_level(CC1101_CS, 0);
    ESP_LOGI(TAG, "Timer wait");
    delay_us(50);
    ESP_LOGI(TAG, "Timer wait");
    gpio_set_level(CC1101_CS, 1);
    delay_us(50);
    gpio_set_level(CC1101_CS, 0);
    ESP_LOGI(TAG, "Wait for miso");
    cc1101_waitMisoLow();

    cc1101_SRES();
    ESP_LOGI(TAG, "CC1101 reset");
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

uint8_t cc1101_readRegister(uint8_t address, ReadRegisterType readType) {
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

    uint32_t in = value;
    uint32_t cmd = address;
    spi_trans_t frame;
    frame.addr = NULL;
    frame.cmd = &cmd;
    frame.mosi = &in;
    frame.miso = NULL;
    frame.bits.cmd = 8;
    frame.bits.addr = 0;
    frame.bits.mosi = 8;
    frame.bits.miso = 0;
    spi_trans(HSPI_HOST, &frame);

    gpio_set_level(CC1101_CS, 1);
}