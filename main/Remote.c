//
// Created by marko on 11.11.2020..
//

#include <esp_log.h>
#include "Remote.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "CC1101.h"
#include <driver/hw_timer.h>
#include <freertos/semphr.h>
#include "esp_attr.h"

volatile uint32_t counter = 0;
volatile uint32_t dataLen = 0;
volatile uint32_t data[2048];
volatile bool nextState = true;

#define CC1101_DATAPIN GPIO_NUM_5
static const char *logName = "Remote";

typedef enum {
    SILVER_START,
    SILVER_START_END,
    SILVER_HEADER,
    SILVER_HEADER_END,
    SILVER_SEND_NEXT_BIT,
    SILVER_SENDING1,
    SILVER_SENDING0,
    SILVER_END,
    SILVER_END_END
} SilvercrestState;

typedef struct {
    uint8_t repeatCount;
    uint32_t silvercrestCommand;
    uint8_t commandBitCounter;
    SilvercrestState state;
} Silver;

typedef enum {
    VOLT_HEADER,
    VOLT_HEADER_END,
    VOLT_REPEAT,
    VOLT_NEXT_GROUP,
    VOLT_SENDING_GROUP,
    VOLT_SENDING_GROUP_1
} VoltomatState;

typedef struct {
    uint8_t repeatCounter;
    uint64_t command;
    uint64_t commandLeftover;
    uint8_t commandBitCounter;
    uint8_t groupCounter;
    VoltomatState state;
} VOLTOMAT;

typedef struct {
    SemaphoreHandle_t semaphore;
    Silver *silver;
    VOLTOMAT *voltomat;
} remote_t;

remote_t *remote;

void IRAM_ATTR timerSilverCrest() {
    Silver *s = remote->silver;
    switch (s->state) {
        case SILVER_START:
            gpio_set_level(CC1101_DATAPIN, 0);
            s->state = SILVER_START_END;
            hw_timer_alarm_us(320, false);
            break;
        case SILVER_START_END:
            gpio_set_level(CC1101_DATAPIN, 1);
            s->state = SILVER_HEADER;
            hw_timer_alarm_us(2350, false);
            break;
        case SILVER_HEADER:
            gpio_set_level(CC1101_DATAPIN, 0);
            s->state = SILVER_HEADER_END;
            hw_timer_alarm_us(320, false);
            break;
        case SILVER_HEADER_END:
            gpio_set_level(CC1101_DATAPIN, 1);
            s->state = SILVER_SEND_NEXT_BIT;
            hw_timer_alarm_us(1250, false);
            break;
        case SILVER_SEND_NEXT_BIT:
            if (s->commandBitCounter == 1) {
                if (s->repeatCount > 1) {
                    s->repeatCount--;
                    s->state = SILVER_START;
                    s->commandBitCounter = 24;
                    timerSilverCrest();
                    return;
                }
                gpio_set_level(CC1101_DATAPIN, 0);
                s->state = SILVER_END;
                hw_timer_alarm_us(400, false);
                return;
            }
            uint32_t currentValue = s->silvercrestCommand & (1 << (s->commandBitCounter - 1));
            s->commandBitCounter--;
            // 1 should be sent as 1100us high, 400us low
            // 0 should be sent as 400us high, 1100 low
            if (currentValue > 0) {
                gpio_set_level(CC1101_DATAPIN, 0);
                s->state = SILVER_SENDING1;
                hw_timer_alarm_us(1100, false);
            } else {
                gpio_set_level(CC1101_DATAPIN, 0);
                s->state = SILVER_SENDING0;
                hw_timer_alarm_us(400, false);
            }
            break;
        case SILVER_SENDING1:
            gpio_set_level(CC1101_DATAPIN, 1);
            s->state = SILVER_SEND_NEXT_BIT;
            hw_timer_alarm_us(400, false);
            break;
        case SILVER_SENDING0:
            gpio_set_level(CC1101_DATAPIN, 1);
            s->state = SILVER_SEND_NEXT_BIT;
            hw_timer_alarm_us(1100, false);
            break;
        case SILVER_END:
            gpio_set_level(CC1101_DATAPIN, 1);
            s->state = SILVER_END_END;
            hw_timer_alarm_us(2300, false);
            break;
        case SILVER_END_END:
            gpio_set_level(CC1101_DATAPIN, 0);
            // timer stop
            xSemaphoreGive(remote->semaphore);
            break;
    }
}

void IRAM_ATTR timerVoltomat() {
    VOLTOMAT *v = remote->voltomat;
    switch (v->state) {
        case VOLT_HEADER:
            gpio_set_level(CC1101_DATAPIN, 0);
            v->state = VOLT_HEADER_END;
            hw_timer_alarm_us(300, false);
            break;
        case VOLT_HEADER_END:
            gpio_set_level(CC1101_DATAPIN, 1);
            v->state = VOLT_NEXT_GROUP;
            hw_timer_alarm_us(2560, false);
            break;
        case VOLT_REPEAT:
            gpio_set_level(CC1101_DATAPIN, 1);
            v->state = VOLT_HEADER;
            hw_timer_alarm_us(10000, false);
            break;
        case VOLT_NEXT_GROUP:
            gpio_set_level(CC1101_DATAPIN, 0);
            if (v->commandBitCounter > 0) {
                v->groupCounter = (v->commandLeftover % 3) + 1;
                v->commandLeftover /= 3;
                v->commandBitCounter--;
                v->state = VOLT_SENDING_GROUP;
                hw_timer_alarm_us(300, false);
                return;
            }

            if(v->repeatCounter > 1) {
                v->repeatCounter--;
                v->state = VOLT_REPEAT;
                v->commandLeftover = v->command;
                v->commandBitCounter = 33;
                hw_timer_alarm_us(300, false);
                return;
            }

            xSemaphoreGive(remote->semaphore);
            break;
        case VOLT_SENDING_GROUP:
            gpio_set_level(CC1101_DATAPIN, 1);
            if(v->groupCounter > 1) {
                v->groupCounter--;
                v->state = VOLT_SENDING_GROUP_1;
                hw_timer_alarm_us(220, false);
            } else {
                v->state = VOLT_NEXT_GROUP;
                hw_timer_alarm_us(1300, false);
            }
            break;
        case VOLT_SENDING_GROUP_1:
            gpio_set_level(CC1101_DATAPIN, 0);
            v->state = VOLT_SENDING_GROUP;
            hw_timer_alarm_us(300, false);
            break;
    }
}

esp_err_t runSilverCrestCommand(uint32_t silvercrestCommand) {
    ESP_LOGI(logName, "Starting silver command");
    if (cc1101_startTX() != ESP_OK) {
        ESP_LOGE(logName, "Could not start TX");
        return ESP_FAIL;
    }
    if (hw_timer_init(timerSilverCrest, NULL) != ESP_OK) {
        ESP_LOGE(logName, "Could not initialize hardware timer");
        return ESP_FAIL;
    }
    hw_timer_disarm();
    Silver *s = malloc(sizeof(Silver));
    remote->silver = s;
    s->state = SILVER_START;
    s->silvercrestCommand = silvercrestCommand;
    s->commandBitCounter = 24;
    s->repeatCount = 5;
    hw_timer_alarm_us(15, false);
    xSemaphoreTake(remote->semaphore, portMAX_DELAY);
    hw_timer_disarm();
    hw_timer_deinit();
    cc1101_stop();
    free(s);
    remote->silver = NULL;
    ESP_LOGI(logName, "Finished silver command");
    return ESP_OK;
}

esp_err_t runVoltomatCommand(uint64_t command) {
    ESP_LOGI(logName, "Starting voltomat command");
    if (cc1101_startTX() != ESP_OK) {
        ESP_LOGE(logName, "Could not start TX");
        return ESP_FAIL;
    }
    if (hw_timer_init(timerVoltomat, NULL) != ESP_OK) {
        ESP_LOGE(logName, "Could not initialize hardware timer");
        return ESP_FAIL;
    }
    hw_timer_disarm();
    VOLTOMAT *v = malloc(sizeof(VOLTOMAT));
    remote->voltomat = v;
    v->state = VOLT_HEADER;
    v->commandBitCounter = 33;
    v->command = command;
    v->commandLeftover = command;
    v->repeatCounter = 5;
    hw_timer_alarm_us(15, false);
    xSemaphoreTake(remote->semaphore, portMAX_DELAY);
    hw_timer_disarm();
    hw_timer_deinit();
    cc1101_stop();
    ESP_LOGI(logName, "Finished voltomat command");
    return ESP_OK;
}

void remote_initialize() {
    remote = heap_caps_malloc(sizeof(remote_t), MALLOC_CAP_8BIT);
    remote->semaphore = xSemaphoreCreateBinary();
    remote_sendingCommand = false;
    gpio_config_t cc1101Output;
    cc1101Output.mode = GPIO_MODE_OUTPUT;
    cc1101Output.pin_bit_mask = 1 << CC1101_DATAPIN;
    cc1101Output.intr_type = GPIO_INTR_DISABLE;
    cc1101Output.pull_up_en = GPIO_PULLUP_ENABLE;
    cc1101Output.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&cc1101Output);
    cc1101_initialize();
}

uint32_t remote_startSend(uint32_t srcData[], uint32_t srcDataLen) {
    if (remote_sendingCommand == true)
        return -1;
    printf("command started\n");
    remote_sendingCommand = true;
    if (cc1101_startTX() != 0) {
        remote_sendingCommand = 0;
        return -1;
    }
    printf("tx started\n");
    // copy data
    dataLen = srcDataLen;
    for (uint32_t i = 0; i < srcDataLen; i++) {
        data[i] = srcData[i];
    }

    // initialize send sequence and start sending
    counter = 0;
    nextState = false;

    for (uint32_t i = 0; i < dataLen; i++) {
        gpio_set_level(CC1101_DATAPIN, nextState);
        nextState = !nextState;
        vTaskDelay(data[i] / portTICK_PERIOD_MS);
    }
    printf("stopping cc1101\n");
    cc1101_stop();
    remote_sendingCommand = false;
    return 0;
}

uint32_t remote_sendCmdVoltomat(uint64_t cmd) {
    return runVoltomatCommand(cmd);
}

uint32_t remote_sendCmdSilverCrest(uint32_t cmd) {
    return runSilverCrestCommand(cmd);
}

void remote_writeOutData() {
    printf("Write out data\n");
    for (uint32_t i = 0; i < dataLen; i++) {
        printf("%d\n", data[i]);
    }
}

/*

Voltomat
base - 213122222223221222222

222232
ch1 - 122222 212222
ch2 - 122231 212231
ch3 - 122312 212312
ch4 - 122321 212321

231
all - 222222 312222

Converted to decimal number (64bit)
 decimal numbers were created (left to right)
ch1 - 2772751969796374 2757500774826400
ch2 - 1537405177228480 1522153982258506
ch3 - 2360969705607076 2345718510637102
ch4 - 1125622913039182 1110371718069208
all - 2777835701453032 2762584506483058

SilverCrest (32bit)
ch1 - 1349248 863008
ch2 - 1638120 266280
ch3 - 1638136 2073944
ch4 - 2073924 1638116
all - 2073940 1638132

*/