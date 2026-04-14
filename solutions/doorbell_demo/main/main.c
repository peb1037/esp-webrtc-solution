/* Door Bell Demo

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include <time.h>
#include "argtable3/argtable3.h"
#include "esp_console.h"
#include "esp_webrtc.h"
#include "media_lib_adapter.h"
#include "media_lib_os.h"
#include "esp_timer.h"
#include "webrtc_utils_time.h"
#include "esp_cpu.h"
#include "settings.h"
#include "common.h"
#include "esp_capture.h"
#include "cloud/cloud_ctrl.h"

static const char *TAG = "Webrtc_Test";

static struct {
    struct arg_str *room_id;
    struct arg_end *end;
} room_args;

#if !WEBRTC_USE_LIVEKIT_WHIP
static char room_url[128];
#endif

static char server_url[64] = "https://webrtc.espressif.com";

static void ensure_time_is_set(void)
{
    static bool sntp_started = false;
    if (!sntp_started) {
        webrtc_utils_time_sync_init();
        sntp_started = true;
    }

    time_t now = 0;
    time(&now);
    if (now < 1700000000) {
        webrtc_utils_wait_for_time_sync(10000);
        time(&now);
    }
    ESP_LOGI(TAG, "System time (epoch): %ld", (long)now);
}

#if !WEBRTC_USE_LIVEKIT_WHIP
static void log_room_hint(const char *room)
{
    if (room == NULL || room[0] == 0) {
        return;
    }
    ESP_LOGW(TAG, "Room: %s  |  Open: %s/doorbell", room, server_url);
}
#endif

#define RUN_ASYNC(name, body)           \
    void run_async##name(void *arg)     \
    {                                   \
        body;                           \
        media_lib_thread_destroy(NULL); \
    }                                   \
    media_lib_thread_create_from_scheduler(NULL, #name, run_async##name, NULL);

static int join_room(int argc, char **argv)
{
#if WEBRTC_USE_LIVEKIT_WHIP
    (void)argc;
    (void)argv;
    if (LIVEKIT_WHIP_URL[0] == 0) {
        ESP_LOGE(TAG, "LIVEKIT_WHIP_URL is empty (see settings.h)");
        return -1;
    }
    ensure_time_is_set();
    ESP_LOGI(TAG, "Starting WHIP ingest: %s", LIVEKIT_WHIP_URL);
    return start_webrtc((char *)LIVEKIT_WHIP_URL);
#else
    int nerrors = arg_parse(argc, argv, (void **)&room_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, room_args.end, argv[0]);
        return 1;
    }
    static bool sntp_synced = false;
    if (sntp_synced == false) {
        if (0 == webrtc_utils_time_sync_init()) {
            sntp_synced = true;
        }
    }
    const char *room_id = room_args.room_id->sval[0];
    snprintf(room_url, sizeof(room_url), "%s/join/%s", server_url, room_id);
    ESP_LOGI(TAG, "Start to join in room %s", room_id);
    if (start_webrtc(room_url) == 0) {
        log_room_hint(room_id);
    } else {
        ESP_LOGE(TAG, "Failed to start WebRTC for room %s", room_id);
        log_room_hint(room_id);
    }
    return 0;
#endif
}

static int leave_room(int argc, char **argv)
{
    RUN_ASYNC(leave, { stop_webrtc(); });
    return 0;
}

static int cmd_cli(int argc, char **argv)
{
    send_cmd(argc > 1 ? argv[1] : "ring");
    return 0;
}

static int assert_cli(int argc, char **argv)
{
    *(int *)0 = 0;
    return 0;
}

static int sys_cli(int argc, char **argv)
{
    sys_state_show();
    return 0;
}

static int wifi_cli(int argc, char **argv)
{
    if (argc < 2) {
        ESP_LOGE(TAG, "Usage: wifi <ssid...> [password]");
        ESP_LOGE(TAG, "Tip: if SSID contains spaces, quotes are optional (wifi joins tokens)");
        return -1;
    }

    const char *password = NULL;
    int ssid_tokens = argc - 1;
    if (argc >= 3) {
        password = argv[argc - 1];
        ssid_tokens = argc - 2;
    }

    const char *ssid = NULL;
    char ssid_buf[128] = { 0 };
    if (ssid_tokens <= 1) {
        ssid = argv[1];
    } else {
        size_t used = 0;
        for (int i = 0; i < ssid_tokens; i++) {
            const char *tok = argv[1 + i];
            if (!tok) {
                continue;
            }
            int written = snprintf(ssid_buf + used, sizeof(ssid_buf) - used, "%s%s", (i == 0) ? "" : " ", tok);
            if (written <= 0) {
                break;
            }
            used += (size_t)written;
            if (used >= sizeof(ssid_buf)) {
                used = sizeof(ssid_buf) - 1;
                break;
            }
        }
        ssid = ssid_buf;
    }

    ESP_LOGI(TAG, "Wi-Fi connect request: ssid=\"%s\" (len=%d), password=%s", ssid, (int)strlen(ssid), password ? "set" : "<open>");
    return network_connect_wifi(ssid, password);
}

static int server_cli(int argc, char **argv)
{
    int server_sel = argc > 1 ? atoi(argv[1]) : 0;
    if (server_sel == 0) {
        strcpy(server_url, "https://webrtc.espressif.com");
    } else {
        strcpy(server_url, "https://webrtc.espressif.cn");
    }
    ESP_LOGI(TAG, "Select server %s", server_url);
    return 0;
}

static int bitrate_cli(int argc, char **argv)
{
    bool is_audio = false;
    int bitrate = 0;
    if (argc < 2) {
        return -1;
    } else if (argc == 2) {
        is_audio = true;
        bitrate = atoi(argv[1]);
    } else {
        if (strcmp(argv[1], "audio") == 0 || argv[1][0] == '1') {
            is_audio = true;
        }
        bitrate = atoi(argv[2]);
    }
    return set_webrtc_bitrate(is_audio, bitrate);
}

static int capture_to_player_cli(int argc, char **argv)
{
    return test_capture_to_player();
}

static int measure_cli(int argc, char **argv)
{
    void measure_enable(bool enable);
    void show_measure(void);
    measure_enable(true);
    media_lib_thread_sleep(1500);
    measure_enable(false);
    return 0;
}

static int init_console()
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "esp>";
    repl_config.task_stack_size = 10 * 1024;
    repl_config.task_priority = 22;
    repl_config.max_cmdline_length = 1024;
    // install console REPL environment
#if CONFIG_ESP_CONSOLE_UART
    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));
#elif CONFIG_ESP_CONSOLE_USB_CDC
    esp_console_dev_usb_cdc_config_t cdc_config = ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_cdc(&cdc_config, &repl_config, &repl));
#elif CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    esp_console_dev_usb_serial_jtag_config_t usbjtag_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&usbjtag_config, &repl_config, &repl));
#endif

    room_args.room_id = arg_str1(NULL, NULL, "<w123456>", "room name");
    room_args.end = arg_end(2);
    esp_console_cmd_t cmds[] = {
        {
            .command = "join",
            .help = "Please enter a room name.\r\n",
            .func = join_room,
            .argtable = &room_args,
        },
        {
            .command = "leave",
            .help = "Leave from room\n",
            .func = leave_room,
        },
        {
            .command = "cmd",
            .help = "Send command (ring etc)\n",
            .func = cmd_cli,
        },
        {
            .command = "i",
            .help = "Show system status\r\n",
            .func = sys_cli,
        },
        {
            .command = "assert",
            .help = "Assert system\r\n",
            .func = assert_cli,
        },
        {
            .command = "rec2play",
            .help = "Play capture content\n",
            .func = capture_to_player_cli,
        },
        {
            .command = "wifi",
            .help = "wifi ssid psw\r\n",
            .func = wifi_cli,
        },
        {
            .command = "m",
            .help = "measure system loading\r\n",
            .func = measure_cli,
        },
        {
            .command = "server",
            .help = "Select server\r\n",
            .func = server_cli,
        },
         {
            .command = "bitrate",
            .help = "Set audio or video bitrate\r\n",
            .func = bitrate_cli,
        },
    };
    for (int i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
    return 0;
}

static void thread_scheduler(const char *thread_name, media_lib_thread_cfg_t *schedule_cfg)
{
    if (strcmp(thread_name, "venc_0") == 0) {
        // For H264 may need huge stack if use hardware encoder can set it to small value
        schedule_cfg->priority = 10;
#if CONFIG_IDF_TARGET_ESP32S3
        schedule_cfg->stack_size = 20 * 1024;
#endif
    }
#ifdef WEBRTC_SUPPORT_OPUS
    else if (strcmp(thread_name, "aenc_0") == 0) {
        // For OPUS encoder it need huge stack, when use G711 can set it to small value
        schedule_cfg->stack_size = 40 * 1024;
        schedule_cfg->priority = 10;
        schedule_cfg->core_id = 1;
    }
    else if (strcmp(thread_name, "Adec") == 0) {
        // For OPUS encoder it need huge stack, when use G711 can set it to small value
        schedule_cfg->stack_size = 40 * 1024;
        schedule_cfg->priority = 10;
        schedule_cfg->core_id = 1;
    }
#endif
    else if (strcmp(thread_name, "AUD_SRC") == 0) {
        schedule_cfg->priority = 15;
    } else if (strcmp(thread_name, "pc_task") == 0) {
        schedule_cfg->stack_size = 25 * 1024;
        schedule_cfg->priority = 18;
        schedule_cfg->core_id = 1;
    }
    if (strcmp(thread_name, "start") == 0) {
        schedule_cfg->stack_size = 6 * 1024;
    }
}

static void capture_scheduler(const char *name, esp_capture_thread_schedule_cfg_t *schedule_cfg)
{
    media_lib_thread_cfg_t cfg = {
        .stack_size = schedule_cfg->stack_size,
        .priority = schedule_cfg->priority,
        .core_id = schedule_cfg->core_id,
    };
    schedule_cfg->stack_in_ext = true;
    thread_scheduler(name, &cfg);
    schedule_cfg->stack_size = cfg.stack_size;
    schedule_cfg->priority = cfg.priority;
    schedule_cfg->core_id = cfg.core_id;
}

#if !WEBRTC_USE_LIVEKIT_WHIP
static char* gen_room_id_use_mac(void)
{
    static char room_mac[16];
    uint8_t mac[6];
    network_get_mac(mac);
    snprintf(room_mac, sizeof(room_mac)-1, "esp_%02x%02x%02x", mac[3], mac[4], mac[5]);
    return room_mac;
}
#endif

static int network_event_handler(bool connected)
{
    if (connected) {
        // Enter into Room directly
        RUN_ASYNC(start, {
            ensure_time_is_set();
            // AWS IoT uses TLS; ensure time is valid before connecting.
            // Run this inside the async thread so the network event handler stays snappy.
            if (network_is_connected()) {
                cloud_ctrl_on_network(true);
            }
#if WEBRTC_USE_LIVEKIT_WHIP
            if (LIVEKIT_WHIP_URL[0] == 0) {
                ESP_LOGE(TAG, "LIVEKIT_WHIP_URL is empty (see settings.h)");
            } else {
                ESP_LOGI(TAG, "Starting WHIP ingest: %s", LIVEKIT_WHIP_URL);
                if (network_is_connected()) {
                    int r = start_webrtc((char *)LIVEKIT_WHIP_URL);
                    ESP_LOGI(TAG, "WHIP start_webrtc() returned: %d", r);
                } else {
                    ESP_LOGW(TAG, "Network disconnected before WHIP start");
                }
            }
#else
            char *room = gen_room_id_use_mac();
            snprintf(room_url, sizeof(room_url), "%s/join/%s", server_url, room);
            ESP_LOGI(TAG, "Start to join in room %s", room);
            if (network_is_connected()) {
                if (start_webrtc(room_url) == 0) {
                    log_room_hint(room);
                } else {
                    ESP_LOGE(TAG, "Failed to start WebRTC, but room is still %s", room);
                    log_room_hint(room);
                }
            }
#endif
        });
    } else {
        cloud_ctrl_on_network(false);
        stop_webrtc();
    }
    return 0;
}

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    // ESP-Hosted / Wi-Fi-Remote stack (ESP32-P4 uses an external Wi-Fi co-processor).
    // These tags help diagnose SDIO transport bring-up and scan/connect behavior.
    esp_log_level_set("H_API", ESP_LOG_INFO);
    esp_log_level_set("transport", ESP_LOG_INFO);
    esp_log_level_set("sdio_wrapper", ESP_LOG_INFO);
    esp_log_level_set("rpc_wrap", ESP_LOG_INFO);
    esp_log_level_set("rpc_evt", ESP_LOG_INFO);
    esp_log_level_set("esp_adapter", ESP_LOG_INFO);
    // Keep the interactive console usable: the Wi-Fi driver can spam warnings like
    // "wifi:m f probe req..." while scanning/connecting.
    esp_log_level_set("wifi", ESP_LOG_ERROR);

    ESP_LOGI(TAG, "FW build: %s %s | IDF: %s", __DATE__, __TIME__, esp_get_idf_version());
    media_lib_add_default_adapter();
    esp_capture_set_thread_scheduler(capture_scheduler);
    media_lib_thread_set_schedule_cb(thread_scheduler);
    init_board();
    if (media_sys_buildup() != 0) {
        ESP_LOGE(TAG, "media_sys_buildup failed, stopping startup");
        while (1) {
            media_lib_thread_sleep(1000);
        }
    }
    init_console();
    cloud_ctrl_init();

    if (strcmp(WIFI_SSID, "XXXX") == 0) {
        ESP_LOGW(TAG, "WIFI_SSID/WIFI_PASSWORD still set to placeholder. Use the CLI: wifi <ssid> <password>");
    }
    ESP_LOGI(TAG, "LiveKit WHIP mode: %s", WEBRTC_USE_LIVEKIT_WHIP ? "ENABLED" : "DISABLED");
#if WEBRTC_USE_LIVEKIT_WHIP
    if (LIVEKIT_WHIP_URL[0] == 0) {
        ESP_LOGW(TAG, "LIVEKIT_WHIP_URL is empty (see settings.h)");
    } else {
        ESP_LOGI(TAG, "WHIP URL: %s", LIVEKIT_WHIP_URL);
    }
#else
    ESP_LOGI(TAG, "Signaling server: %s", server_url);
#endif

    if (AWS_IOT_ENDPOINT[0] == 0 || strstr(AWS_IOT_ENDPOINT, "xxxx") != NULL) {
        ESP_LOGW(TAG, "AWS_IOT_ENDPOINT not configured (see settings.h)");
    } else {
        ESP_LOGI(TAG, "AWS IoT: endpoint=%s cmd=%s evt=%s", AWS_IOT_ENDPOINT, AWS_IOT_TOPIC_CMD, AWS_IOT_TOPIC_EVT);
    }

#if WEBRTC_USE_LIVEKIT_WHIP
    ESP_LOGI(TAG, "Room hint/log URL disabled in WHIP mode (legacy AppRTC flow)");
#else
    char *room = gen_room_id_use_mac();
    ESP_LOGW(TAG, "Auto room (from MAC): %s", room);
    ESP_LOGI(TAG, "Signaling URL: %s/join/%s", server_url, room);
    log_room_hint(room);
#endif

    network_init(WIFI_SSID, WIFI_PASSWORD, network_event_handler);
    while (1) {
        media_lib_thread_sleep(2000);
        query_webrtc();
    }
}
