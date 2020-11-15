//
// Created by marko on 11.11.2020..
//

#ifndef MQTT_TCP_REMOTE_H
#define MQTT_TCP_REMOTE_H

#include <stdint.h>
#include "stdbool.h"

volatile bool remote_sendingCommand;

void remote_initialize();

uint32_t remote_sendCmdVoltomat(uint64_t cmd);

uint32_t remote_sendCmdSilverCrest(uint32_t cmd);

void remote_writeOutData();


#endif //MQTT_TCP_REMOTE_H
