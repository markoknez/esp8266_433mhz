//
// Created by marko on 15.11.2020..
//


#include <esp_err.h>
#include <mqtt_client.h>
#include <esp_log.h>
#include <math.h>
#include "Remote.h"

const static char *TAG = "SEND_QUEUE";

typedef enum {
    SILVERCREST,
    VOLTOMAT
} RemoteCmdType;

typedef struct {
    RemoteCmdType type;
    uint64_t cmd;
} RemoteCmd;

QueueHandle_t remoteCmdQueue;

_Noreturn void sendTask(void *pvTask) {
    RemoteCmd cmd;
    while (true) {
        xQueueReceive(remoteCmdQueue, &cmd, portMAX_DELAY);
        if (cmd.type == SILVERCREST) {
            remote_sendCmdSilverCrest(cmd.cmd);
        } else if (cmd.type == VOLTOMAT) {
            remote_sendCmdVoltomat(cmd.cmd);
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

uint64_t parseUint64(esp_mqtt_event_handle_t event) {
    uint64_t intFromString = 0;
    for (uint32_t i = 0; i < event->data_len; i++) {
        intFromString += (event->data[event->data_len - 1 - i] - 48) * pow(10, i);
    }
    return intFromString;
}

esp_err_t queue_mqttEvent(esp_mqtt_event_handle_t event) {
    esp_mqtt_client_handle_t client = event->client;
    switch(event->event_id) {
        case MQTT_EVENT_ERROR:
            break;
        case MQTT_EVENT_CONNECTED:
            esp_mqtt_client_subscribe(client, "/cmds/p1", 1);
            esp_mqtt_client_subscribe(client, "/cmds/p2", 1);
            esp_mqtt_client_subscribe(client, "/cmds/silver/remote1", 1);
            esp_mqtt_client_subscribe(client, "/cmds/voltomat/remote1", 1);
            break;
        case MQTT_EVENT_DISCONNECTED:
        case MQTT_EVENT_SUBSCRIBED:
        case MQTT_EVENT_UNSUBSCRIBED:
        case MQTT_EVENT_PUBLISHED:
            break;
        case MQTT_EVENT_DATA:
            if(strncmp("/cmds/silver/remote1", event->topic, event->topic_len) == 0) {
                uint8_t cmdIndex = event->data[0] - 48;
                uint32_t cmd = 0;
                RemoteCmd remoteCommand;
                switch(cmdIndex) {
                    case 0: cmd = 1349248; break;
                    case 1: cmd = 863008; break;
                    case 2: cmd = 1638120; break;
                    case 3: cmd = 266280; break;
                    case 4: cmd = 1638136; break;
                    case 5: cmd = 2073944; break;
                    case 6: cmd = 2073924; break;
                    case 7: cmd = 1638116; break;
                    case 8: cmd = 2073940; break;
                    case 9: cmd = 1638132; break;
                    default:
                        ESP_LOGE(TAG, "Invalid argument for topic %.*s", event->topic_len, event->topic);
                        return ESP_OK;
                }
                remoteCommand.cmd = cmd;
                remoteCommand.type = SILVERCREST;
                xQueueSend(remoteCmdQueue, &remoteCommand, 0);
            } else if(strncmp("/cmds/voltomat/remote1", event->topic, event->topic_len) == 0) {
                uint8_t cmdIndex = event->data[0] - 48;
                uint64_t cmd = 0;
                RemoteCmd remoteCommand;
                switch(cmdIndex) {
                    case 0: cmd = 2772751969796374; break;
                    case 1: cmd = 2757500774826400; break;
                    case 2: cmd = 1537405177228480; break;
                    case 3: cmd = 1522153982258506; break;
                    case 4: cmd = 2360969705607076; break;
                    case 5: cmd = 2345718510637102; break;
                    case 6: cmd = 1125622913039182; break;
                    case 7: cmd = 1110371718069208; break;
                    case 8: cmd = 2777835701453032; break;
                    case 9: cmd = 2762584506483058; break;
                    default:
                        ESP_LOGE(TAG, "Invalid argument for topic %.*s", event->topic_len, event->topic);
                        return ESP_OK;
                }
                remoteCommand.cmd = cmd;
                remoteCommand.type = VOLTOMAT;
                xQueueSend(remoteCmdQueue, &remoteCommand, 0);
            } else if(strncmp("/cmds/silver", event->topic, event->topic_len) == 0) {
                uint64_t cmd = parseUint64(event);
                RemoteCmd remoteCommand;
                remoteCommand.cmd = cmd;
                remoteCommand.type = SILVERCREST;
                xQueueSend(remoteCmdQueue, &remoteCommand, 0);
            } else if(strncmp("/cmds/voltomat", event->topic, event->topic_len) == 0) {
                uint64_t cmd = parseUint64(event);
                RemoteCmd remoteCommand;
                remoteCommand.cmd = cmd;
                remoteCommand.type = VOLTOMAT;
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
