//
// Created by marko on 11.11.2020..
//

#ifndef MQTT_TCP_REMOTE_H
#define MQTT_TCP_REMOTE_H

#include <stdint.h>
#include <esp_err.h>
#include "stdbool.h"

volatile bool remote_sendingCommand;

esp_err_t remote_initialize();

esp_err_t remote_sendCmdVoltomat(uint64_t cmd);

esp_err_t remote_sendCmdSilverCrest(uint32_t cmd);

esp_err_t remote_sendVoglauer(uint64_t cmd);

esp_err_t remote_resetCC1101();

void remote_writeOutData();


#endif //MQTT_TCP_REMOTE_H
