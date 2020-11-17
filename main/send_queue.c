//
// Created by marko on 15.11.2020..
//


#include <esp_err.h>
#include <mqtt_client.h>
#include <esp_log.h>
#include "Remote.h"

const static char *TAG = "SEND_QUEUE";

typedef enum {
    SILVERCREST,
    Voltomat,
    Voglauer
} RemoteCmdType;

typedef struct {
    RemoteCmdType type;
    uint64_t cmd;
    uint8_t repeat;
} RemoteCmd;

QueueHandle_t remoteCmdQueue;

_Noreturn void sendTask(void *pvTask) {
    RemoteCmd cmd;
    while (true) {
        xQueueReceive(remoteCmdQueue, &cmd, portMAX_DELAY);
        switch (cmd.type) {
            case SILVERCREST:
                remote_sendCmdSilverCrest(cmd.cmd);
                break;
            case Voltomat:
                remote_sendCmdVoltomat(cmd.cmd);
                break;
            case Voglauer:
                remote_sendVoglauer(cmd.cmd);
                break;
            default:
                ESP_LOGE(TAG, "Not implemented");
        }
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

esp_err_t queue_init() {
    remote_initialize();
    remoteCmdQueue = xQueueCreate(10, sizeof(RemoteCmd));
    if (remoteCmdQueue == NULL) {
        ESP_LOGE(TAG, "Could not create queue");
        abort();
    }
    xTaskCreate(sendTask, "send", 10240, NULL, 5, NULL);
    return ESP_OK;
}

void print(uint64_t val) {
    uint64_t c = val;
    while (c > 0) {
        printf("%u", (uint32_t)(c % 10));
        c /= 10;
    }
    printf("\n");
}

uint64_t pow10(uint8_t val) {
    uint64_t returnValue = 1;
    for (uint8_t i = 0; i < val; i++) {
        returnValue *= 10;
    }
    return returnValue;
}

uint64_t parseUint64(const char *ch, uint32_t len) {
    uint64_t intFromString = 0;
    for (uint32_t i = 0; i < len; i++) {
        uint64_t value = ch[len - 1 - i] - 48;
        intFromString += (uint64_t) value * (uint64_t) pow10(i);
    }
    return intFromString;
}

esp_err_t queue_mqttEvent(esp_mqtt_event_handle_t event) {
    esp_mqtt_client_handle_t client = event->client;
    switch (event->event_id) {
        case MQTT_EVENT_ERROR:
            break;
        case MQTT_EVENT_CONNECTED:
            esp_mqtt_client_subscribe(client, "remote/silver", 1);
            esp_mqtt_client_subscribe(client, "remote/silver/1", 1);
            esp_mqtt_client_subscribe(client, "remote/voltomat", 1);
            esp_mqtt_client_subscribe(client, "remote/voltomat/1", 1);
            esp_mqtt_client_subscribe(client, "remote/voglauer", 1);
            break;
        case MQTT_EVENT_DISCONNECTED:
        case MQTT_EVENT_SUBSCRIBED:
        case MQTT_EVENT_UNSUBSCRIBED:
        case MQTT_EVENT_PUBLISHED:
            break;
        case MQTT_EVENT_DATA:
            if (strncmp("remote/silver/1", event->topic, event->topic_len) == 0) {
                uint8_t cmdIndex = event->data[0] - 48;
                uint32_t cmd = 0;
                RemoteCmd remoteCommand;
                switch (cmdIndex) {
                    case 0:
                        cmd = 1349248;
                        break;
                    case 1:
                        cmd = 863008;
                        break;
                    case 2:
                        cmd = 1638120;
                        break;
                    case 3:
                        cmd = 266280;
                        break;
                    case 4:
                        cmd = 1638136;
                        break;
                    case 5:
                        cmd = 2073944;
                        break;
                    case 6:
                        cmd = 2073924;
                        break;
                    case 7:
                        cmd = 1638116;
                        break;
                    case 8:
                        cmd = 2073940;
                        break;
                    case 9:
                        cmd = 1638132;
                        break;
                    default:
                        ESP_LOGE(TAG, "Invalid argument for topic %.*s", event->topic_len, event->topic);
                        return ESP_OK;
                }
                remoteCommand.cmd = cmd;
                remoteCommand.type = SILVERCREST;
                remoteCommand.repeat = 5;
                xQueueSend(remoteCmdQueue, &remoteCommand, 0);
            } else if (strncmp("remote/voltomat/1", event->topic, event->topic_len) == 0) {
                uint8_t cmdIndex = event->data[0] - 48;
                uint64_t cmd = 0;
                RemoteCmd remoteCommand;
                switch (cmdIndex) {
                    case 0:
                        cmd = 2772751969796374;
                        break;
                    case 1:
                        cmd = 2757500774826400;
                        break;
                    case 2:
                        cmd = 1537405177228480;
                        break;
                    case 3:
                        cmd = 1522153982258506;
                        break;
                    case 4:
                        cmd = 2360969705607076;
                        break;
                    case 5:
                        cmd = 2345718510637102;
                        break;
                    case 6:
                        cmd = 1125622913039182;
                        break;
                    case 7:
                        cmd = 1110371718069208;
                        break;
                    case 8:
                        cmd = 2777835701453032;
                        break;
                    case 9:
                        cmd = 2762584506483058;
                        break;
                    default:
                        ESP_LOGE(TAG, "Invalid argument for topic %.*s", event->topic_len, event->topic);
                        return ESP_OK;
                }
                remoteCommand.cmd = cmd;
                remoteCommand.type = Voltomat;
                remoteCommand.repeat = 5;
                xQueueSend(remoteCmdQueue, &remoteCommand, 0);
            } else if (strncmp("remote/silver", event->topic, event->topic_len) == 0) {
                RemoteCmd remoteCommand;
                uint64_t cmd = parseUint64(event->data, event->data_len);
                remoteCommand.cmd = cmd;
                remoteCommand.type = SILVERCREST;
                remoteCommand.repeat = 5;
                xQueueSend(remoteCmdQueue, &remoteCommand, 0);
            } else if (strncmp("remote/voltomat", event->topic, event->topic_len) == 0) {
                RemoteCmd remoteCommand;
                uint64_t cmd = parseUint64(event->data, event->data_len);
                remoteCommand.cmd = cmd;
                remoteCommand.type = Voltomat;
                remoteCommand.repeat = 5;
                xQueueSend(remoteCmdQueue, &remoteCommand, 0);
            } else if (strncmp("remote/voglauer", event->topic, event->topic_len) == 0) {
                char *lengthStr = memchr(event->data, ',', event->data_len);
                if(lengthStr == NULL) {
                    ESP_LOGW(TAG, "Invalid voglauer command");
                    return ESP_OK;
                }
                uint64_t command = parseUint64(event->data, lengthStr - event->data);
                lengthStr++; // move pointer over ,
                uint8_t repeat = (uint8_t)parseUint64(lengthStr, event->data + event->data_len - lengthStr);
                printf("Command:");
                print(command);
                printf("Repeat:");
                print(repeat);

                RemoteCmd remoteCommand;
                remoteCommand.cmd = command;
                remoteCommand.type = Voglauer;
                remoteCommand.repeat = repeat;
                xQueueSend(remoteCmdQueue, &remoteCommand, 0);
            } else {
                ESP_LOGW(TAG, "Could not find handler for topic %.*s", event->topic_len, event->topic);
            }
            break;
        case MQTT_EVENT_BEFORE_CONNECT:
            break;
    }
    return ESP_OK;
}
