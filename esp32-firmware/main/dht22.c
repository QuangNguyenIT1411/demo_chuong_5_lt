#include "dht22.h"
#include <stdio.h>
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DHT22";
static gpio_num_t dht_pin;

// Waits until 'pin' reaches 'level', or returns -1 after 'timeout_us' microseconds.
// On success, returns how long we waited (in microseconds).
static int wait_for_level(gpio_num_t pin, int level, int timeout_us)
{
    int64_t start = esp_timer_get_time();
    while (gpio_get_level(pin) != level) {
        if (esp_timer_get_time() - start > timeout_us) {
            return -1;
        }
    }
    return (int)(esp_timer_get_time() - start);
}

void dht22_init(gpio_num_t pin)
{
    dht_pin = pin;
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_INPUT);
    // Internal pull-up as a safety net; the docs still require an external
    // 4.7-10k resistor to 3V3 on the DATA line for reliable reads.
    gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY);
    gpio_set_level(pin, 1);
    ESP_LOGI(TAG, "DHT22 initialized on GPIO %d", pin);
}

int dht22_read(float *temperature, float *humidity)
{
    uint8_t data[5] = {0, 0, 0, 0, 0};

    // --- 1. Send start signal: MCU pulls the line LOW for >1ms, then releases it ---
    gpio_set_direction(dht_pin, GPIO_MODE_OUTPUT);
    gpio_set_level(dht_pin, 0);
    esp_rom_delay_us(1200);
    gpio_set_level(dht_pin, 1);
    esp_rom_delay_us(30);
    gpio_set_direction(dht_pin, GPIO_MODE_INPUT);

    // Timing below is sensitive to task preemption; keep interrupts on this
    // core disabled for the short duration of the actual bit-banging read.
    portDISABLE_INTERRUPTS();

    // --- 2. Sensor response: LOW ~80us, then HIGH ~80us, then starts sending data ---
    if (wait_for_level(dht_pin, 0, 100) < 0) {
        portENABLE_INTERRUPTS();
        ESP_LOGW(TAG, "Timeout: no response (LOW) from sensor");
        return -1;
    }
    if (wait_for_level(dht_pin, 1, 100) < 0) {
        portENABLE_INTERRUPTS();
        ESP_LOGW(TAG, "Timeout: no response (HIGH) from sensor");
        return -1;
    }
    if (wait_for_level(dht_pin, 0, 100) < 0) {
        portENABLE_INTERRUPTS();
        ESP_LOGW(TAG, "Timeout: sensor did not start sending data");
        return -1;
    }

    // --- 3. Read 40 bits (5 bytes: humidity_h, humidity_l, temp_h, temp_l, checksum) ---
    // Each bit: ~50us LOW, then HIGH whose duration encodes the bit:
    //   ~26-28us HIGH -> bit 0
    //   ~70us    HIGH -> bit 1
    for (int i = 0; i < 40; i++) {
        if (wait_for_level(dht_pin, 1, 100) < 0) {
            portENABLE_INTERRUPTS();
            ESP_LOGW(TAG, "Timeout waiting HIGH edge at bit %d", i);
            return -1;
        }

        int64_t high_start = esp_timer_get_time();
        if (wait_for_level(dht_pin, 0, 100) < 0) {
            portENABLE_INTERRUPTS();
            ESP_LOGW(TAG, "Timeout waiting LOW edge at bit %d", i);
            return -1;
        }
        int64_t high_duration = esp_timer_get_time() - high_start;

        data[i / 8] <<= 1;
        if (high_duration > 40) { // threshold between ~27us (0) and ~70us (1)
            data[i / 8] |= 1;
        }
    }

    portENABLE_INTERRUPTS();

    // --- 4. Verify checksum ---
    uint8_t checksum = (uint8_t)(data[0] + data[1] + data[2] + data[3]);
    if (checksum != data[4]) {
        ESP_LOGW(TAG, "Checksum mismatch: got 0x%02X, expected 0x%02X", data[4], checksum);
        return -1;
    }

    *humidity = ((data[0] << 8) | data[1]) / 10.0f;

    int16_t temp_raw = ((data[2] & 0x7F) << 8) | data[3];
    float temp = temp_raw / 10.0f;
    if (data[2] & 0x80) { // sign bit set -> negative temperature
        temp = -temp;
    }
    *temperature = temp;

    return 0;
}