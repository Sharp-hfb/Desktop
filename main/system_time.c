#include "system_time.h"

#include "esp_log.h"   
#include "esp_err.h"
#include "my_wifi.h"
#include "cJSON.h"
#include "esp_http_client.h"
#include "device_config.h"
#define TAG "system_time"

#define URL_TS "http://acs.m.taobao.com/gw/mtop.common.getTimestamp/"
/// @brief 设置系统时区为中国标准时间
/// @param  
void set_system_time_country(void)
{
    setenv("TZ", "CST-8", 1);
    tzset();
}

/// @brief 设置系统时间
/// @param timestamp 时间戳
void set_system_time(time_t timestamp) {
    struct timeval tv;
    tv.tv_sec = timestamp;   // 秒
    tv.tv_usec = 0;          // 微秒

    if (settimeofday(&tv, NULL) == 0) {
        ESP_LOGI(TAG,"system time set successfully");
    } else {
        ESP_LOGE(TAG,"system time set failed");
    }
}

// HTTP 事件回调（可选，用于打印接收的每个数据块）
static esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA: len=%d", evt->data_len);
        if (evt->data && evt->data_len > 0) {
            //这里是限制打印长度，防止日志过长
            int dump_len = evt->data_len;
            if (dump_len > 256) dump_len = 256;
            char tmp[257];
            memcpy(tmp, evt->data, dump_len);
            tmp[dump_len] = '\0';
            ESP_LOGI(TAG, "chunk: %s", tmp);
            // 解析 JSON 获取时间戳
            cJSON *root = cJSON_Parse(tmp);
            if (!root) {
                ESP_LOGE(TAG, "JSON Parse Error");
                return -1;
            }
             // 获取 data 对象
            cJSON *data = cJSON_GetObjectItem(root, "data");
            if (!data) {
                ESP_LOGE(TAG, "JSON missing 'data'");
                cJSON_Delete(root);
                return -1;
            }
            // 获取 t 字段
            cJSON *t = cJSON_GetObjectItem(data, "t");
            if (!t || !cJSON_IsString(t)) {
                ESP_LOGE(TAG, "JSON missing 't'");
                cJSON_Delete(root);
                return -1;
            }

            // 转为 long long（因为时间戳 > int 范围）
            long long timestamp = atoll(t->valuestring);
            timestamp /= 1000; // 毫秒转秒
            cJSON_Delete(root);
            printf("timestamp: %lld\n", timestamp);
            set_system_time((time_t)timestamp);
            /* 保存到 flash */
            cfgPara.last_timestamp = (uint64_t)timestamp;
            config_save();
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

void  get_network_time()
{   
    if(!g_wifi_is_connected)
    {
        ESP_LOGE(TAG, "wifi is not connected");
        return;
    }
    esp_err_t err;
    esp_http_client_config_t config = {
        .url = URL_TS,
        .method = HTTP_METHOD_GET,
        .event_handler = _http_event_handler,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialise HTTP connection");
        return;
    }

    ESP_LOGI(TAG, "Performing HTTP GET: %s", URL_TS);
    err = esp_http_client_perform(client);
    esp_http_client_cleanup(client);
}