/* Network

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_err.h>
#include <esp_mac.h>
#include <string.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include <stdlib.h>
#include "esp_netif.h"
#include "esp_event.h"
#include "network.h"

#ifdef CONFIG_NETWORK_USE_ETHERNET
#include "esp_eth.h"
#include "ethernet_init.h"
#else
#include <esp_wifi.h>
#endif

#define TAG "NETWORK"

static bool               network_connected = false;
static network_connect_cb connect_cb;

static void network_set_connected(bool connected)
{
    if (network_connected != connected) {
        network_connected = connected;
        if (connect_cb) {
            connect_cb(connected);
        }
    }
}

bool network_is_connected(void)
{
    return network_connected;
}

#ifndef CONFIG_NETWORK_USE_ETHERNET

static wifi_config_t wifi_config;

static bool s_scan_in_progress = false;
static bool s_connect_after_scan = false;
static char s_scan_target_ssid[33] = { 0 };
static bool s_last_scan_passive = false;
static uint8_t s_scan_retry_passive = 0;

static void network_start_scan_for_current_ssid(bool passive)
{
    if (s_scan_in_progress) {
        return;
    }
    const char *ssid = (const char *)wifi_config.sta.ssid;
    if (ssid[0] == 0 || strcmp(ssid, "XXXX") == 0) {
        return;
    }
    strlcpy(s_scan_target_ssid, ssid, sizeof(s_scan_target_ssid));

    wifi_scan_config_t scan_cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
        .scan_type = passive ? WIFI_SCAN_TYPE_PASSIVE : WIFI_SCAN_TYPE_ACTIVE,
    };
    if (passive) {
        // Longer dwell improves discovery on some APs/hotspots.
        scan_cfg.scan_time.passive = 300;
    }
    esp_err_t err = esp_wifi_scan_start(&scan_cfg, false);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to start Wi-Fi scan (%s)", esp_err_to_name(err));
        return;
    }
    s_scan_in_progress = true;
    s_last_scan_passive = passive;
    ESP_LOGI(TAG, "Scanning for APs (target ssid=\"%s\")...", s_scan_target_ssid);
}

#define PART_NAME     "wifi-set"
#define WIFI_SSID_KEY "ssid"
#define WIFI_PSW_KEY  "psw"

static bool load_from_nvs(void)
{
    nvs_handle_t wifi_nvs = 0;
    bool load_ok = false;
    do {
        esp_err_t ret = nvs_open(PART_NAME, NVS_READWRITE, &wifi_nvs);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Fail to open nvs ret %d", ret);
            break;
        }
        size_t size = sizeof(wifi_config.sta.ssid);
        ret = nvs_get_str(wifi_nvs, WIFI_SSID_KEY, (char*)(wifi_config.sta.ssid), &size);
        if (ret != ESP_OK) {
            break;
        }
        wifi_config.sta.ssid[sizeof(wifi_config.sta.ssid) - 1] = '\0';
        size = sizeof(wifi_config.sta.password);
        ret = nvs_get_str(wifi_nvs, WIFI_PSW_KEY, (char*)(wifi_config.sta.password), &size);
        if (ret != ESP_OK) {
            break;
        }
        wifi_config.sta.password[sizeof(wifi_config.sta.password) - 1] = '\0';
        load_ok = true;
    } while (0);
    if (wifi_nvs) {
        nvs_close(wifi_nvs);
    }
    return load_ok;
}

static void store_to_nvs(void)
{
    nvs_handle_t wifi_nvs = 0;
    do {
        esp_err_t ret = nvs_open(PART_NAME, NVS_READWRITE, &wifi_nvs);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Fail to open nvs ret %d", ret);
            break;
        }
        ret = nvs_set_str(wifi_nvs, WIFI_SSID_KEY, (char*)(wifi_config.sta.ssid));
        if (ret != ESP_OK) {
            break;
        }
        ret = nvs_set_str(wifi_nvs, WIFI_PSW_KEY, (char*)(wifi_config.sta.password));
        if (ret != ESP_OK) {
            break;
        }

        ret = nvs_commit(wifi_nvs);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Fail to commit wifi nvs ret %d", ret);
            break;
        }
    } while (0);
    if (wifi_nvs) {
        nvs_close(wifi_nvs);
    }
}

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        const char *ssid = (const char *)wifi_config.sta.ssid;
        if (ssid[0] == 0 || strcmp(ssid, "XXXX") == 0) {
            ESP_LOGW(TAG, "Wi-Fi not configured yet. Use: wifi <ssid...> [password]");
            return;
        }
        ESP_LOGI(TAG, "Connecting to SSID: \"%s\"", ssid);
        // Scan first (non-blocking) to improve diagnostics when connection fails.
        s_connect_after_scan = true;
        network_start_scan_for_current_ssid(false);
        if (!s_scan_in_progress) {
            // If scan couldn't start, try connecting anyway.
            esp_wifi_connect();
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (event_data) {
            const wifi_event_sta_disconnected_t *disc = (const wifi_event_sta_disconnected_t *)event_data;
            char ssid[33] = { 0 };
            if (disc->ssid_len > 0) {
                size_t n = disc->ssid_len;
                if (n > sizeof(ssid) - 1) {
                    n = sizeof(ssid) - 1;
                }
                memcpy(ssid, disc->ssid, n);
                ssid[n] = 0;
            }
            ESP_LOGW(TAG,
                     "STA disconnected: reason=%d, rssi=%d, ssid=\"%s\", bssid=%02x:%02x:%02x:%02x:%02x:%02x",
                     disc->reason, disc->rssi, ssid,
                     disc->bssid[0], disc->bssid[1], disc->bssid[2], disc->bssid[3], disc->bssid[4], disc->bssid[5]);

            // 201 is commonly WIFI_REASON_NO_AP_FOUND. Start a scan so we can print what APs are visible.
            if (disc->reason == 201) {
                network_start_scan_for_current_ssid(false);
            }
        }
        network_set_connected(false);
        const char *ssid = (const char *)wifi_config.sta.ssid;
        if (ssid[0] == 0 || strcmp(ssid, "XXXX") == 0) {
            // Don't spam reconnect attempts when credentials aren't configured.
            ESP_LOGW(TAG, "Wi-Fi not configured yet. Use: wifi <ssid...> [password]");
            return;
        }
        esp_wifi_connect();
        ESP_LOGI(TAG, "retry to connect to the AP");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        s_scan_in_progress = false;

        if (event_data) {
            const wifi_event_sta_scan_done_t *sd = (const wifi_event_sta_scan_done_t *)event_data;
            ESP_LOGI(TAG, "Scan done: status=%u number=%u passive=%s",
                     (unsigned)sd->status, (unsigned)sd->number, s_last_scan_passive ? "yes" : "no");
        }

        uint16_t ap_count = 0;
        esp_err_t err = esp_wifi_scan_get_ap_num(&ap_count);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Scan done but failed to get AP count (%s)", esp_err_to_name(err));
            ap_count = 0;
        }
        if (ap_count == 0) {
            ESP_LOGW(TAG, "Scan done: no APs found");
            // Retry once with passive scan in case active probing is ineffective.
            if (!s_last_scan_passive && s_scan_retry_passive == 0) {
                s_scan_retry_passive = 1;
                ESP_LOGW(TAG, "Retrying scan with PASSIVE mode");
                network_start_scan_for_current_ssid(true);
            }
        } else {
            s_scan_retry_passive = 0;
            const uint16_t max_print = 20;
            uint16_t to_fetch = ap_count > max_print ? max_print : ap_count;
            wifi_ap_record_t *recs = (wifi_ap_record_t *)calloc(to_fetch, sizeof(wifi_ap_record_t));
            if (!recs) {
                ESP_LOGW(TAG, "Scan done: OOM fetching %u AP records", (unsigned)to_fetch);
            } else {
                uint16_t fetched = to_fetch;
                err = esp_wifi_scan_get_ap_records(&fetched, recs);
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "Scan done but failed to get AP records (%s)", esp_err_to_name(err));
                } else {
                    bool found_target = false;
                    ESP_LOGI(TAG, "Visible APs (showing %u of %u):", (unsigned)fetched, (unsigned)ap_count);
                    for (int i = 0; i < fetched; i++) {
                        const char *seen_ssid = (const char *)recs[i].ssid;
                        ESP_LOGI(TAG, "  ssid=\"%s\" ch=%u rssi=%d auth=%d",
                                 seen_ssid, (unsigned)recs[i].primary, recs[i].rssi, (int)recs[i].authmode);
                        if (s_scan_target_ssid[0] && strcmp(seen_ssid, s_scan_target_ssid) == 0) {
                            found_target = true;
                        }
                    }
                    if (s_scan_target_ssid[0]) {
                        ESP_LOGW(TAG, "Target SSID \"%s\" %s in scan results",
                                 s_scan_target_ssid, found_target ? "WAS FOUND" : "NOT FOUND");
                    }
                }
                free(recs);
            }
        }

        if (s_connect_after_scan) {
            s_connect_after_scan = false;
            esp_wifi_connect();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        network_set_connected(true);
        store_to_nvs();
    }
}

int network_init(const char *ssid, const char *password, network_connect_cb cb)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    // Keep console usable: suppress noisy Wi-Fi driver warnings (e.g. probe req spam)
    // while still allowing ERROR logs through.
    esp_log_level_set("wifi", ESP_LOG_ERROR);

    // Some phone hotspots (and some regions) commonly use 2.4GHz channels 12/13.
    // If we stay on a restricted default, scans can return 0 APs.
    wifi_country_t country = {
        .cc = "01", // world-safe
        .schan = 1,
        .nchan = 13,
        .policy = WIFI_COUNTRY_POLICY_AUTO,
    };
    esp_err_t c_ret = esp_wifi_set_country(&country);
    if (c_ret != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_set_country failed (%s)", esp_err_to_name(c_ret));
    }
    wifi_country_t got = { 0 };
    if (esp_wifi_get_country(&got) == ESP_OK) {
        ESP_LOGI(TAG, "Wi-Fi country: cc=%s schan=%u nchan=%u policy=%u", got.cc, (unsigned)got.schan, (unsigned)got.nchan, (unsigned)got.policy);
    }

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    if (load_from_nvs()) {
        ESP_LOGI(TAG, "Force to use wifi config from nvs");
    } else {
        if (ssid) {
            strlcpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
        }
        if (password) {
            strlcpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
        }
    }
    connect_cb = cb;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "wifi_init_sta finished.");
    return 0;
}

int network_get_mac(uint8_t mac[6])
{
    if (!mac) {
        return -1;
    }

    // On some targets/setups (e.g. ESP32-P4 with hosted Wi-Fi), ESP_MAC_WIFI_STA may not exist.
    // Prefer STA MAC when available for stable room naming, but gracefully fall back.
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (err == ESP_OK) {
        return 0;
    }
    err = esp_read_mac(mac, ESP_MAC_BASE);
    if (err == ESP_OK) {
        return 0;
    }

    ESP_LOGW(TAG, "esp_read_mac failed (%s); using 00:00:00:00:00:00", esp_err_to_name(err));
    memset(mac, 0, 6);
    return -1;
}

int network_connect_wifi(const char *ssid, const char *password)
{
    // Ensure Wi-Fi driver warnings don't flood the console during connect attempts.
    esp_log_level_set("wifi", ESP_LOG_ERROR);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    if (ssid) {
        strlcpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    }
    if (password) {
        strlcpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
    } else {
        wifi_config.sta.password[0] = 0;
    }
    ESP_LOGI(TAG, "wifi_connect: ssid=\"%s\" (len=%d)", (char *)wifi_config.sta.ssid, (int)strlen((char *)wifi_config.sta.ssid));
    network_connected = false;
    esp_wifi_disconnect();
    esp_wifi_stop();
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    return 0;
}

#else
static esp_eth_handle_t *eth_handles = NULL;
static void eth_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    uint8_t mac_addr[6] = { 0 };
    /* we can get the ethernet driver handle from event data */
    esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;

    switch (event_id) {
        case ETHERNET_EVENT_CONNECTED:
            esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
            ESP_LOGI(TAG, "Ethernet Link Up");
            ESP_LOGI(TAG, "Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x",
                     mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
            break;
        case ETHERNET_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "Ethernet Link Down");
            network_set_connected(false);
            break;
        case ETHERNET_EVENT_START:
            ESP_LOGI(TAG, "Ethernet Started");
            break;
        case ETHERNET_EVENT_STOP:
            ESP_LOGI(TAG, "Ethernet Stopped");
            break;
        default:
            break;
    }
}

/** Event handler for IP_EVENT_ETH_GOT_IP */
static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    const esp_netif_ip_info_t *ip_info = &event->ip_info;

    ESP_LOGI(TAG, "Ethernet Got IP Address");
    ESP_LOGI(TAG, "~~~~~~~~~~~");
    ESP_LOGI(TAG, "ETHIP:" IPSTR, IP2STR(&ip_info->ip));
    ESP_LOGI(TAG, "ETHMASK:" IPSTR, IP2STR(&ip_info->netmask));
    ESP_LOGI(TAG, "ETHGW:" IPSTR, IP2STR(&ip_info->gw));
    ESP_LOGI(TAG, "~~~~~~~~~~~");
    network_set_connected(true);
}

int network_init(const char *ssid, const char *password, network_connect_cb cb)
{
    // Initialize Ethernet driver
    uint8_t eth_port_cnt = 0;
    ESP_ERROR_CHECK(example_eth_init(&eth_handles, &eth_port_cnt));

    // Initialize TCP/IP network interface aka the esp-netif (should be called only once in application)
    ESP_ERROR_CHECK(esp_netif_init());
    // Create default event loop that running in background
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create instance(s) of esp-netif for Ethernet(s)
    if (eth_port_cnt == 1) {
        // Use ESP_NETIF_DEFAULT_ETH when just one Ethernet interface is used and you don't need to modify
        // default esp-netif configuration parameters.
        esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
        esp_netif_t *eth_netif = esp_netif_new(&cfg);
        // Attach Ethernet driver to TCP/IP stack
        ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handles[0])));
    } else {
        // Use ESP_NETIF_INHERENT_DEFAULT_ETH when multiple Ethernet interfaces are used and so you need to modify
        // esp-netif configuration parameters for each interface (name, priority, etc.).
        esp_netif_inherent_config_t esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_ETH();
        esp_netif_config_t cfg_spi = {
            .base = &esp_netif_config,
            .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH
        };
        char if_key_str[10];
        char if_desc_str[10];
        char num_str[3];
        for (int i = 0; i < eth_port_cnt; i++) {
            itoa(i, num_str, 10);
            strcat(strcpy(if_key_str, "ETH_"), num_str);
            strcat(strcpy(if_desc_str, "eth"), num_str);
            esp_netif_config.if_key = if_key_str;
            esp_netif_config.if_desc = if_desc_str;
            esp_netif_config.route_prio -= i * 5;
            esp_netif_t *eth_netif = esp_netif_new(&cfg_spi);

            // Attach Ethernet driver to TCP/IP stack
            ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handles[i])));
        }
    }

    // Register user defined event handers
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL));
    connect_cb = cb;
    // Start Ethernet driver state machine
    for (int i = 0; i < eth_port_cnt; i++) {
        ESP_ERROR_CHECK(esp_eth_start(eth_handles[i]));
    }
    return 0;
}

int network_get_mac(uint8_t mac[6])
{
    if (eth_handles) {
        esp_eth_ioctl(eth_handles[0], ETH_CMD_G_MAC_ADDR, mac);
    }
    return 0;
}

int network_connect_wifi(const char *ssid, const char *password)
{
    ESP_LOGE(TAG, "Using ethernet now not support wifi config");
    return 0;
}

#endif
