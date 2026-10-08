#ifdef __cplusplus
extern "C" {
#endif
}

#include "freertos/FreeRTOS.h"

#include "esp_event.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_wifi.h"

#include "nvs_flash.h"
#include "string.h"

uint8_t beacon_raw[] = {
    0x80, 0x00, // 0-1: Frame Control
    0x00, 0x00, // 2-3: Duration
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, // 4-9: Destination address (broadcast)
    0xba, 0xde, 0xaf, 0xfe, 0x00, 0x06, // 10-15: Source address
    0xba, 0xde, 0xaf, 0xfe, 0x00, 0x06, // 16-21: BSSID
    0x00, 0x00, // 22-23: Sequence / fragment number
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, // 24-31: Timestamp (GETS OVERWRITTEN TO 0 BY HARDWARE)
    0x64, 0x00, // 32-33: Beacon interval
    0x31, 0x04, // 34-35: Capability info
    0x00, 0x00, /* FILL CONTENT HERE */ // 36-38: SSID parameter set, 0x00:length:content
    0x01, 0x08, 0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24, // 39-48: Supported rates
    0x03, 0x01, 0x01, // 49-51: DS Parameter set, current channel 1 (= 0x01),
    0x05, 0x04, 0x01, 0x02, 0x00, 0x00, // 52-57: Traffic Indication Map

};

// char *rick_ssids[] = {
//     "01 Never gonna give you up",
//     "02 Never gonna let you down",
//     "03 Never gonna run around",
//     "04 and desert you",
//     "05 Never gonna make you cry",
//     "06 Never gonna say goodbye",
//     "07 Never gonna tell a lie",
//     "08 and hurt you"
// };

#pragma pack(push, 1)
struct uint24 {
    uint32_t value: 24;
};
struct uint48 {
    uint64_t value : 48;
};
#pragma pack(pop)

uint24 bssid_prefixes[] = {
    0x0026b4, // Ford
    0x0076b6, // Ford
    0x0001a9, // BMW
    0xe01333, // General Motors
    0x8c1f64, // General Motors
    0x001A11, // Google, Inc.
    0x00F620, // Google, Inc.
    0x04006E, // Google, Inc.
    0x04C8B0, // Google, Inc.
    0xA4B805, // Apple, Inc.
    0x903C92, // Apple, Inc.
    0xE47684, // Apple, Inc.
    0x1878D4, // Verizon
    0x20C047, // Verizon
    0x485D36, // Verizon
};

char *ssid_prefixes[] = {
    "Verizon-MiFi",
    "Pixel_",
    "iPhone ",
    "DIRECT-BMW ",
    "My BMW Hotspot ",
    "Ford_F150_WiFi_",
    "SYNC_Hotspot_",
    "Lexus_Hotspot_"
};

#define UNIQUE_ID "435330"
#define BEACON_SSID_OFFSET 38
#define SRCADDR_OFFSET 10
#define BSSID_OFFSET 16
#define SEQNUM_OFFSET 22
#define TOTAL_BSSIDS (sizeof(bssid_prefixes) / sizeof(uint24))
#define TOTAL_SSIDS (sizeof(ssid_prefixes) / sizeof(char*))

char *generate_ssid() {
    char ssid[80];
    strcpy(ssid, ssid_prefixes[esp_random() % TOTAL_SSIDS]);
    strcpy(ssid, UNIQUE_ID);
    return ssid;
}

uint48 generate_bssid() {
    uint24 prefix = bssid_prefixes[esp_random() % TOTAL_BSSIDS];
    uint48 bssid{};
    bssid.value = ((uint64_t)prefix.value << 24) | (esp_random() & 0xFFFFFF);
    return bssid;
}

const void *generate_frame(char *ssid, uint48 bssid) {
    uint8_t frame[200];
    memcpy(frame, beacon_raw, BEACON_SSID_OFFSET - 1);

    frame[BEACON_SSID_OFFSET - 1] = strlen(ssid);
    memcpy(&frame[BEACON_SSID_OFFSET], ssid, strlen(ssid));
    memcpy(&frame[BEACON_SSID_OFFSET + strlen(ssid)], &beacon_raw[BEACON_SSID_OFFSET], sizeof(beacon_raw) - BEACON_SSID_OFFSET);

    frame[SRCADDR_OFFSET] = bssid.value & 0xffffffffffff;
    frame[BSSID_OFFSET] = bssid.value & 0xffffffffffff;

    frame[SEQNUM_OFFSET] = (esp_random() % 10 & 0x0f) << 4;
    frame[SEQNUM_OFFSET + 1] = (esp_random() % 10 & 0xff0) >> 4;

    return frame;
}

void spam_task(void *pvParameter) {
    for (;;) {
        vTaskDelay(100 / TOTAL_SSIDS / portTICK_PERIOD_MS);

        char* ssid = generate_ssid();
        uint48 bssid = generate_bssid();

        esp_wifi_80211_tx(WIFI_IF_AP, generate_frame(ssid, bssid), sizeof(beacon_raw) + strlen(ssid), false);
    }
}

void setup(void) {
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
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
}
