#ifdef __cplusplus
extern "C" {
#endif
}

#include "Arduino.h"
#include "freertos/FreeRTOS.h"

#include "esp_event.h"
#include "esp_system.h"
#include "esp_wifi.h"

#include "nvs_flash.h"
#include "string.h"

#define LED 2

// @formatter:off
/* clang-format off */
uint8_t beacon_raw[] = {
  0x80, 0x00, // 0-1: Frame Control
  0x00, 0x00, // 2-3: Duration
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, // 4-9: Destination address (broadcast)
  0xba, 0xde, 0xaf, 0xfe, 0x00, 0x06, // 10-15: Source address
  0xba, 0xde, 0xaf, 0xfe, 0x00, 0x06, // 16-21: BSSID
  0x00, 0x00, // 22-23: Sequence / fragment number
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, // 24-31: Timestamp (overwritten by ESP32 TX hardware)
  0x64, 0x00, // 32-33: Beacon interval
  0x31, 0x04, // 34-35: Capability info
  0x00, 0x00, /* FILL CONTENT HERE */ // 36-38: SSID parameter set, 0x00:length:content
  0x01, 0x08, 0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24, // 39-48: Supported rates
  0x03, 0x01, 0x01, // 49-51: DS Parameter set, current channel 1 (= 0x01),
  0x05, 0x04, 0x01, 0x02, 0x00, 0x00, // 52-57: Traffic Indication Map
};
/* clang-format on */
// @formatter:on

struct Oui {
    uint8_t bytes[3];
};

struct MacAddr {
    uint8_t bytes[6];
};

Oui bssid_prefixes[] = {
    {{0x00, 0x26, 0xb4}}, // Ford
    {{0x00, 0x76, 0xb6}}, // Ford
    {{0x00, 0x01, 0xa9}}, // BMW
    {{0xe0, 0x13, 0x33}}, // General Motors
    {{0x8c, 0x1f, 0x64}}, // General Motors
    {{0x00, 0x1A, 0x11}}, // Google, Inc.
    {{0x00, 0xF6, 0x20}}, // Google, Inc.
    {{0x04, 0x00, 0x6E}}, // Google, Inc.
    {{0x04, 0xC8, 0xB0}}, // Google, Inc.
    {{0xA4, 0xB8, 0x05}}, // Apple, Inc.
    {{0x90, 0x3C, 0x92}}, // Apple, Inc.
    {{0xE4, 0x76, 0x84}}, // Apple, Inc.
    {{0x18, 0x78, 0xD4}}, // Verizon
    {{0x20, 0xC0, 0x47}}, // Verizon
    {{0x48, 0x5D, 0x36}}, // Verizon
};

char *ssid_prefixes[] = {
    "Verizon-MiFi", "Pixel_", "iPhone ",
    "DIRECT-BMW ", "My BMW Hotspot ", "Ford_F150_WiFi_",
    "SYNC_Hotspot_", "Lexus_Hotspot_"
};

#define UNIQUE_ID "435330"
#define BEACON_SSID_OFFSET 38
#define SRCADDR_OFFSET 10
#define BSSID_OFFSET 16
#define SEQNUM_OFFSET 22
#define MAX_SSID_LEN 32
#define FRAME_BUF_SIZE (sizeof(beacon_raw) + MAX_SSID_LEN)
#define TOTAL_BSSIDS (sizeof(bssid_prefixes) / sizeof(bssid_prefixes[0]))
#define TOTAL_SSIDS (sizeof(ssid_prefixes) / sizeof(ssid_prefixes[0]))

static void generate_ssid(char *out, size_t out_size) {
    snprintf(out, out_size, "%s%s%u", ssid_prefixes[esp_random() % TOTAL_SSIDS],
             UNIQUE_ID, esp_random() % 9);
}

static MacAddr generate_bssid() {
    const Oui &prefix = bssid_prefixes[esp_random() % TOTAL_BSSIDS];
    MacAddr mac{};
    mac.bytes[0] = prefix.bytes[0];
    mac.bytes[1] = prefix.bytes[1];
    mac.bytes[2] = prefix.bytes[2];
    mac.bytes[3] = (uint8_t) (esp_random() & 0xFF);
    mac.bytes[4] = (uint8_t) (esp_random() & 0xFF);
    mac.bytes[5] = (uint8_t) (esp_random() & 0xFF);
    return mac;
}

static int generate_frame(uint8_t *frame, const size_t frame_cap,
                          const char *ssid, const MacAddr &bssid) {
    size_t ssid_len = strlen(ssid);
    size_t total_len = sizeof(beacon_raw) + ssid_len;
    if (total_len > frame_cap)
        return -1;

    memcpy(frame, beacon_raw, BEACON_SSID_OFFSET - 1);

    frame[BEACON_SSID_OFFSET - 1] = (uint8_t) ssid_len;
    memcpy(&frame[BEACON_SSID_OFFSET], ssid, ssid_len);
    memcpy(&frame[BEACON_SSID_OFFSET + ssid_len], &beacon_raw[BEACON_SSID_OFFSET],
           sizeof(beacon_raw) - BEACON_SSID_OFFSET);

    memcpy(&frame[SRCADDR_OFFSET], bssid.bytes, 6);
    memcpy(&frame[BSSID_OFFSET], bssid.bytes, 6);

    frame[SEQNUM_OFFSET] = (esp_random() % 10 & 0x0f) << 4;
    frame[SEQNUM_OFFSET + 1] = (esp_random() % 10 & 0xff0) >> 4;

    return (int) total_len;
}

void spam_task(void *pvParameter) {
    char ssid[MAX_SSID_LEN + 1];
    uint8_t frame[FRAME_BUF_SIZE];
    for (;;) {
        vTaskDelay(100 / TOTAL_SSIDS / portTICK_PERIOD_MS);

        generate_ssid(ssid, sizeof(ssid));
        MacAddr bssid = generate_bssid();

        int len = generate_frame(frame, sizeof(frame), ssid, bssid);
        if (len < 0) {
            continue;
        }

        esp_wifi_80211_tx(WIFI_IF_AP, frame, len, false);
        Serial.print("TX: ssid = ");
        Serial.print(ssid);
        Serial.print(" ; bssid = 0x");
        Serial.print(bssid.bytes[0], HEX);
        Serial.print(bssid.bytes[1], HEX);
        Serial.print(bssid.bytes[2], HEX);
        Serial.print(bssid.bytes[3], HEX);
        Serial.print(bssid.bytes[4], HEX);
        Serial.println(bssid.bytes[5], HEX);
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("NodeMCU_Spam starting");
    pinMode(LED, OUTPUT);
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    // Init dummy AP to specify a channel and get WiFi hardware into
    // a mode where we can send the actual fake beacon frames.
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    wifi_config_t ap_config = {
        {
            "esp32-beaconspam", // ssd
            "dummypassword", // password
            0, // ssid_len
            1, // channel
            WIFI_AUTH_WPA2_PSK, // authmode
            1, // ssid_hidden
            4, // max_connection
            60000 // beacon_interval
        }
    };

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    xTaskCreate(&spam_task, "spam_task", 4096, NULL, 5, NULL);
}

void loop() {
    digitalWrite(LED, HIGH);
    delay(500);
    digitalWrite(LED, LOW);
    delay(500);
}
