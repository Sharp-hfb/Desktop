#include "my_wifi.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "string.h"
#include "device_config.h"
#include "deep_sleep.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "lwip/err.h"

#include "app_config.h"
#include "app_getweather.h"
#include "my_http_server.h"

#define TAG "my_wifi"
SemaphoreHandle_t wifi_connected_semaphore = NULL;
bool g_wifi_is_got_ip = false,g_wifi_is_connected = false;

static void weather_task(void *arg)
{

    get_network_weather();
    vTaskDelete(NULL);
}
// WiFi事件处理函数
static void wifi_event_handler(void* arg, esp_event_base_t event_base,int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT) 
    {
        switch (event_id)
        {
            case WIFI_EVENT_STA_START:
                //有存储wifi并且不处于配网模式
                if(strlen(cfgPara.staN) > 0 && cfgPara.is_wifi_config_mode == 0)
                {
                    wifi_config_t wifi_config = {0};
                    strcpy((char *)wifi_config.sta.ssid, cfgPara.staN);
                    strcpy((char *)wifi_config.sta.password, cfgPara.staP);
                    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
                    esp_wifi_connect();
                }
                break;
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI("WIFI_EVENT", "WIFI_EVENT_STA_CONNECTED");
                g_wifi_is_connected = true;
                xSemaphoreGive(wifi_connected_semaphore);
                
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                //不处于配网模式下才进行重连操作
                if(cfgPara.is_wifi_config_mode == 0)
                {
                    static int s_retry_num = 0;
                    if (s_retry_num < 3) {
                    esp_wifi_connect();
                    s_retry_num++;
                    ESP_LOGI("wifi", "retry to connect to the AP");
                    } else {
                        ESP_LOGE("wifi", " connect error");
                    }
                    ESP_LOGI("wifi","connect to the AP fail");
                    //wifi断开，ip丢失
                    g_wifi_is_got_ip = false;
                    g_wifi_is_connected = false;
                }
                break;
            default:
                break;
        }
    }
    if(event_base == IP_EVENT)
    {
        switch(event_id)
        {
            case IP_EVENT_STA_GOT_IP:
                xTaskCreate(weather_task, "weather_task", 8192, NULL, 5, NULL);
                if(cfgPara.is_wifi_config_mode)
                {
                    ESP_LOGI("WIFI_EVENT", "WIFI_EVENT_STA_GOT_IP");
                    xSemaphoreGive(wifi_connected_semaphore);
                    g_wifi_is_got_ip = true;
                    
                }
                break;
            default:
                break;
        }
    }
}

void wifi_init(void)
{

    esp_netif_init();
    esp_event_loop_create_default();

    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    assert(sta_netif);

    wifi_connected_semaphore = xSemaphoreCreateBinary();
    if (wifi_connected_semaphore == NULL) {
        ESP_LOGE(TAG, "创建WiFi连接信号量失败");
        return;
    }
    // 注册事件处理器
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    /* 设置静态IP */
    esp_netif_ip_info_t ipInfo;
    IP4_ADDR(&ipInfo.ip, 192, 168, 4, 1);     // AP 自身 IP 地址
    IP4_ADDR(&ipInfo.gw, 192, 168, 4, 1);     // 网关（通常与自身 IP 相同）
    IP4_ADDR(&ipInfo.netmask, 255, 255, 255, 0); // 子网掩码
    // 停止 DHCP 服务（因为要用静态 IP）
    esp_netif_dhcps_stop(ap_netif);
    // 应用静态 IP
    esp_netif_set_ip_info(ap_netif, &ipInfo);
    // 重新启动 DHCP 服务
    esp_netif_dhcps_start(ap_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    if(cfgPara.is_wifi_config_mode)
    {
        ESP_LOGW("WIFI","Enter wifi config mode");
        esp_wifi_set_mode(WIFI_MODE_APSTA);
        change_deep_sleep_timer_period(WIFI_CONFIG_TIMEOUT);
    }
    else
    {
        esp_wifi_set_mode(WIFI_MODE_STA);
    }
    wifi_config_t ap_config = {
        .ap = {
            .ssid = "ESP32_Config_AP",
            .ssid_len = 0,
            .channel = 1,
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN
        },
    };
    esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    esp_wifi_start();
}

void enter_wifi_config_mode_reset()
{
    cfgPara.is_wifi_config_mode = 1;
    config_save();
    esp_restart();
}

void exit_wifi_config_mode(void)
{
    if (cfgPara.is_wifi_config_mode == 0) {
        return;
    }

    ESP_LOGI(TAG, "Exit wifi config mode");
    cfgPara.is_wifi_config_mode = 0;
    config_save();

    stop_webserver();
    esp_wifi_disconnect();
    esp_wifi_set_mode(WIFI_MODE_STA);
    g_wifi_is_got_ip = false;
    g_wifi_is_connected = false;

    if (strlen(cfgPara.staN) > 0) {
        wifi_config_t wifi_config = {0};
        strcpy((char *)wifi_config.sta.ssid, cfgPara.staN);
        strcpy((char *)wifi_config.sta.password, cfgPara.staP);
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        esp_wifi_connect();
    }

    change_deep_sleep_timer_period(ENTER_DEEP_SLEEP_TIME);
}

bool wifi_is_got_ip()
{
    return g_wifi_is_got_ip;
}
