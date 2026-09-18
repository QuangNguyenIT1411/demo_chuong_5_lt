#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <inttypes.h>

#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "protocol_examples_common.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_log.h"
#include "mqtt_client.h"
#include "driver/gpio.h"
#include "cJSON.h"
#include "dht22.h"

static const char *TAG = "IOT_ESP32";

#define DEVICE_ID "esp32-001"
#define MQTT_BROKER_URL "mqtt://192.168.50.174:1883"

#define DHT_PIN GPIO_NUM_4
#define LED_PIN GPIO_NUM_5
#define STATUS_HEARTBEAT_INTERVAL_MS 1500

static bool led_state = false;
static volatile bool mqtt_connected = false;
static esp_mqtt_client_handle_t mqtt_client;


// ---------- Timestamp helpers (SNTP -> ISO 8601 UTC) ----------

static void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Time synchronized via SNTP");
}

static void sntp_init_time(void)
{
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();

    // Wait (bounded) for time to be set
    time_t now = 0;
    struct tm timeinfo = {0};

    int retry = 0;
    const int retry_count = 15;

    while (timeinfo.tm_year < (2020 - 1900) && ++retry < retry_count) {
        ESP_LOGI(TAG,
                 "Waiting for system time to be set... (%d/%d)",
                 retry,
                 retry_count);

        vTaskDelay(pdMS_TO_TICKS(2000));

        time(&now);
        localtime_r(&now, &timeinfo);
    }
}

// Writes an ISO 8601 UTC timestamp like 2026-09-13T08:30:00Z
// into buf (buf should be >= 25 bytes).
static void get_iso8601_utc(char *buf, size_t buf_len)
{
    time_t now;
    struct tm timeinfo;

    time(&now);
    gmtime_r(&now, &timeinfo);

    strftime(buf, buf_len, "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
}


// ---------- MQTT ----------

static void publish_online_status(esp_mqtt_client_handle_t client)
{
    char status_topic[100];
    snprintf(status_topic,
             sizeof(status_topic),
             "device/%s/status",
             DEVICE_ID);

    char ts[32];
    get_iso8601_utc(ts, sizeof(ts));

    char status_payload[200];
    snprintf(status_payload,
             sizeof(status_payload),
             "{\"deviceId\":\"%s\",\"status\":\"ONLINE\",\"timestamp\":\"%s\"}",
             DEVICE_ID,
             ts);

    esp_mqtt_client_publish(client,
                            status_topic,
                            status_payload,
                            0,
                            1,
                            1);
}

// Publishes an ACK for a given commandId/action,
// reflecting current led_state.
static void publish_command_ack(esp_mqtt_client_handle_t client,
                                const char *command_id,
                                const char *action)
{
    char ack_topic[100];

    snprintf(ack_topic,
             sizeof(ack_topic),
             "device/%s/command/ack",
             DEVICE_ID);

    char ts[32];
    get_iso8601_utc(ts, sizeof(ts));

    cJSON *root = cJSON_CreateObject();

    cJSON_AddStringToObject(root,
                            "commandId",
                            command_id ? command_id : "");

    cJSON_AddStringToObject(root,
                            "deviceId",
                            DEVICE_ID);

    cJSON_AddStringToObject(root,
                            "action",
                            action ? action : "");

    cJSON_AddStringToObject(root,
                            "status",
                            "ACKNOWLEDGED");

    cJSON_AddBoolToObject(root,
                          "led",
                          led_state);

    cJSON_AddStringToObject(root,
                            "timestamp",
                            ts);

    char *payload = cJSON_PrintUnformatted(root);

    if (payload != NULL) {
        esp_mqtt_client_publish(client,
                                ack_topic,
                                payload,
                                0,
                                1,
                                0);

        ESP_LOGI(TAG,
                 "Sent ACK for command %s: %s",
                 command_id ? command_id : "?",
                 payload);

        cJSON_free(payload);
    }

    cJSON_Delete(root);
}

static void mqtt_event_handler(void *handler_args,
                               esp_event_base_t base,
                               int32_t event_id,
                               void *event_data)
{
    ESP_LOGD(TAG,
             "Event dispatched from event loop base=%s, event_id=%" PRIi32,
             base,
             event_id);

    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t)event_id) {

    case MQTT_EVENT_CONNECTED: {
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");

        mqtt_connected = true;
        publish_online_status(client);

        ESP_LOGI(TAG, "Published ONLINE status");

        // Subscribe command topic
        char cmd_topic[100];

        snprintf(cmd_topic,
                 sizeof(cmd_topic),
                 "device/%s/command",
                 DEVICE_ID);

        esp_mqtt_client_subscribe(client,
                                  cmd_topic,
                                  1);

        ESP_LOGI(TAG,
                 "Subscribed %s",
                 cmd_topic);

        break;
    }

    case MQTT_EVENT_DISCONNECTED:
        mqtt_connected = false;
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_DATA: {
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");

        if (event->data_len > 0) {
            char payload[256];

            int len =
                event->data_len < (int)sizeof(payload) - 1
                    ? event->data_len
                    : (int)sizeof(payload) - 1;

            memcpy(payload,
                   event->data,
                   len);

            payload[len] = '\0';

            cJSON *root = cJSON_Parse(payload);

            if (root == NULL) {
                ESP_LOGE(TAG,
                         "Failed to parse command JSON: %s",
                         payload);
                break;
            }

            const cJSON *action_json =
                cJSON_GetObjectItemCaseSensitive(root,
                                                 "action");

            const cJSON *command_id_json =
                cJSON_GetObjectItemCaseSensitive(root,
                                                 "commandId");

            const char *action =
                (cJSON_IsString(action_json) &&
                 action_json->valuestring != NULL)
                    ? action_json->valuestring
                    : NULL;

            const char *command_id =
                (cJSON_IsString(command_id_json) &&
                 command_id_json->valuestring != NULL)
                    ? command_id_json->valuestring
                    : NULL;

            if (action != NULL) {

                if (strcmp(action, "LED_ON") == 0) {
                    led_state = true;

                    gpio_set_level(LED_PIN, 1);

                    ESP_LOGI(TAG,
                             "Received LED_ON");

                } else if (strcmp(action, "LED_OFF") == 0) {
                    led_state = false;

                    gpio_set_level(LED_PIN, 0);

                    ESP_LOGI(TAG,
                             "Received LED_OFF");

                } else {
                    ESP_LOGW(TAG,
                             "Unknown action: %s",
                             action);
                }

                publish_command_ack(client,
                                    command_id,
                                    action);
            }

            cJSON_Delete(root);
        }

        break;
    }

    default:
        break;
    }
}


// ---------- MQTT startup + Last Will ----------

static void mqtt_app_start(void)
{
    // Last Will topic
    char lwt_topic[100];

    snprintf(lwt_topic,
             sizeof(lwt_topic),
             "device/%s/status",
             DEVICE_ID);

    // Last Will payload.
    // The broker publishes this payload if ESP32 loses MQTT connection
    // unexpectedly (power loss / Wi-Fi loss).
    char lwt_payload[200];

    char ts[32];
    get_iso8601_utc(ts, sizeof(ts));

    snprintf(lwt_payload,
             sizeof(lwt_payload),
             "{\"deviceId\":\"%s\",\"status\":\"OFFLINE\",\"timestamp\":\"%s\"}",
             DEVICE_ID,
             ts);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URL,

        .credentials.client_id = DEVICE_ID,

        .session = {
            // Last Will remains the broker-side fallback for an abrupt loss.
            .keepalive = 5,

            .last_will = {
                .topic = lwt_topic,
                .msg = lwt_payload,
                .qos = 1,
                .retain = true,
            },
        },
    };

    mqtt_client =
        esp_mqtt_client_init(&mqtt_cfg);

    if (mqtt_client == NULL) {
        ESP_LOGE(TAG,
                 "Failed to initialize MQTT client");
        return;
    }

    esp_mqtt_client_register_event(mqtt_client,
                                   ESP_EVENT_ANY_ID,
                                   mqtt_event_handler,
                                   NULL);

    esp_mqtt_client_start(mqtt_client);
}

static void status_heartbeat_task(void *pvParameters)
{
    while (1) {
        if (mqtt_connected) {
            publish_online_status(mqtt_client);
        }

        vTaskDelay(pdMS_TO_TICKS(STATUS_HEARTBEAT_INTERVAL_MS));
    }
}


// ---------- Telemetry ----------

void telemetry_task(void *pvParameters)
{
    float temp;
    float hum;

    char topic[100];
    char payload[256];

    snprintf(topic,
             sizeof(topic),
             "device/%s/telemetry",
             DEVICE_ID);

    while (1) {

        if (dht22_read(&temp, &hum) == 0) {

            char ts[32];
            get_iso8601_utc(ts,
                            sizeof(ts));

            snprintf(payload,
                     sizeof(payload),
                     "{\"deviceId\":\"%s\","
                     "\"temperature\":%.1f,"
                     "\"humidity\":%.1f,"
                     "\"illuminance\":null,"
                     "\"soilMoisture\":null,"
                     "\"led\":%s,"
                     "\"timestamp\":\"%s\"}",
                     DEVICE_ID,
                     temp,
                     hum,
                     led_state ? "true" : "false",
                     ts);

            esp_mqtt_client_publish(mqtt_client,
                                    topic,
                                    payload,
                                    0,
                                    0,
                                    0);

            ESP_LOGI(TAG,
                     "Published Telemetry: T=%.1f H=%.1f",
                     temp,
                     hum);

        } else {

            ESP_LOGW(TAG,
                     "DHT22 read failed, skipping this cycle");
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}


// ---------- Main ----------

void app_main(void)
{
    ESP_LOGI(TAG,
             "[APP] Startup..");

    ESP_LOGI(TAG,
             "[APP] Free memory: %" PRIu32 " bytes",
             esp_get_free_heap_size());

    ESP_LOGI(TAG,
             "[APP] IDF version: %s",
             esp_get_idf_version());

    ESP_ERROR_CHECK(nvs_flash_init());

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(
        esp_event_loop_create_default()
    );

    // Connect Wi-Fi
    ESP_ERROR_CHECK(
        example_connect()
    );

    // Sync real time BEFORE MQTT payloads are generated
    sntp_init_time();

    // LED
    gpio_set_direction(LED_PIN,
                       GPIO_MODE_OUTPUT);

    gpio_set_level(LED_PIN,
                   0);

    // DHT22
    dht22_init(DHT_PIN);

    // MQTT
    mqtt_app_start();

    // Telemetry task
    xTaskCreate(telemetry_task,
                "telemetry_task",
                4096,
                NULL,
                5,
                NULL);

    xTaskCreate(status_heartbeat_task,
                "status_heartbeat_task",
                3072,
                NULL,
                5,
                NULL);
}
