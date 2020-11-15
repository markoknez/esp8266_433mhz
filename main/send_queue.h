//
// Created by marko on 15.11.2020..
//

#ifndef MQTT_TCP_SEND_QUEUE_H

#include <esp_err.h>
#include <mqtt_client.h>


esp_err_t queue_init();
esp_err_t queue_mqttEvent(esp_mqtt_event_handle_t event);

#define MQTT_TCP_SEND_QUEUE_H

#endif //MQTT_TCP_SEND_QUEUE_H
