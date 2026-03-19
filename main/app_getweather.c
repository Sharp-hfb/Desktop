#include "app_getweather.h"

#include "esp_log.h"
#include "esp_err.h"
#include "my_wifi.h"
#include "cJSON.h"
#include "esp_http_client.h"
#include <stdlib.h>
#include <string.h>

#define TAG "app_getweather"

// #define URL_TS "https://api.seniverse.com/v3/weather/daily.json?key=Sqh0Lj6xcWShKnElL&location=beijing&language=zh-Hans&unit=c&start=0&days=1"
#define URL_TS "http://api.seniverse.com/v3/weather/daily.json?key=Sqh0Lj6xcWShKnElL&location=beijing&language=zh-Hans&unit=c&start=0&days=1"

WeatherResponse g_weather_response = {0};


struct response_chunk {
    char *buf;
    size_t len;
    size_t cap;
};

static esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    struct response_chunk *r = evt->user_data;

    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (evt->data == NULL || evt->data_len == 0) break;
        if (r == NULL) {
            ESP_LOGW(TAG, "no user buffer provided");
            break;
        }
        /* ensure capacity */
        if (r->len + evt->data_len + 1 > r->cap) {
            size_t new_cap = r->cap ? r->cap * 2 : 1024;
            while (new_cap < r->len + evt->data_len + 1) new_cap *= 2;
            char *p = realloc(r->buf, new_cap);
            if (p == NULL) {
                ESP_LOGE(TAG, "realloc failed");
                return ESP_ERR_NO_MEM;
            }
            r->buf = p;
            r->cap = new_cap;
        }
        memcpy(r->buf + r->len, evt->data, evt->data_len);
        r->len += evt->data_len;
        r->buf[r->len] = '\0';
        break;
    default:
        break;
    }
    return ESP_OK;
}

static void parse_weather_json(const char *json)
{
    if (json == NULL) return;
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        ESP_LOGE(TAG, "cJSON_Parse failed");
        return;
    }

    cJSON *results = cJSON_GetObjectItem(root, "results");
    if (!cJSON_IsArray(results)) {
        ESP_LOGE(TAG, "no results array");
        cJSON_Delete(root);
        return;
    }

    cJSON *first = cJSON_GetArrayItem(results, 0);
    if (!first) {
        ESP_LOGE(TAG, "results array empty");
        cJSON_Delete(root);
        return;
    }

    cJSON *daily = cJSON_GetObjectItem(first, "daily");
    if (!cJSON_IsArray(daily)) {
        ESP_LOGE(TAG, "no daily array");
        cJSON_Delete(root);
        return;
    }

    cJSON *day0 = cJSON_GetArrayItem(daily, 0);
    if (!day0) {
        ESP_LOGE(TAG, "daily array empty");
        cJSON_Delete(root);
        return;
    }

    const char *date = cJSON_GetObjectItem(day0, "date") ? cJSON_GetObjectItem(day0, "date")->valuestring : NULL;
    const char *text_day = cJSON_GetObjectItem(day0, "text_day") ? cJSON_GetObjectItem(day0, "text_day")->valuestring : NULL;
    const char *text_night = cJSON_GetObjectItem(day0, "text_night") ? cJSON_GetObjectItem(day0, "text_night")->valuestring : NULL;
    const char *high = cJSON_GetObjectItem(day0, "high") ? cJSON_GetObjectItem(day0, "high")->valuestring : NULL;
    const char *low = cJSON_GetObjectItem(day0, "low") ? cJSON_GetObjectItem(day0, "low")->valuestring : NULL;
    const char *wind_speed = cJSON_GetObjectItem(day0, "wind_speed") ? cJSON_GetObjectItem(day0, "wind_speed")->valuestring : NULL;
    const char *humidity = cJSON_GetObjectItem(day0, "humidity") ? cJSON_GetObjectItem(day0, "humidity")->valuestring : NULL;
    const char *wind_direction = cJSON_GetObjectItem(day0, "wind_direction") ? cJSON_GetObjectItem(day0, "wind_direction")->valuestring : NULL;

    memset(&g_weather_response, 0, sizeof(g_weather_response));
    if (date) {
        strncpy(g_weather_response.forecast.date, date, sizeof(g_weather_response.forecast.date) - 1);
    }
    if (high) {
        strncpy(g_weather_response.forecast.high, high, sizeof(g_weather_response.forecast.high) - 1);
    }
    if (low) {
        strncpy(g_weather_response.forecast.low, low, sizeof(g_weather_response.forecast.low) - 1);
    }
    if (humidity) {
        strncpy(g_weather_response.forecast.humidity, humidity, sizeof(g_weather_response.forecast.humidity) - 1);
    }
    if (text_day) {
        strncpy(g_weather_response.forecast.type, text_day, sizeof(g_weather_response.forecast.type) - 1);
    }
    if (wind_direction) {
        strncpy(g_weather_response.forecast.fx, wind_direction, sizeof(g_weather_response.forecast.fx) - 1);
    }
    if (wind_speed) {
        strncpy(g_weather_response.forecast.fl, wind_speed, sizeof(g_weather_response.forecast.fl) - 1);
    }

    ESP_LOGI(TAG, "Weather: date=%s, day=%s, night=%s, high=%s, low=%s, wind=%s, hum=%s ,wind_direction=%s",
             date ? date : "-",
             text_day ? text_day : "-",
             text_night ? text_night : "-",
             high ? high : "-",
             low ? low : "-",
             wind_speed ? wind_speed : "-",
             humidity ? humidity : "-",
             wind_direction ? wind_direction : "-");

    cJSON_Delete(root);
}

void get_network_weather()
{
    esp_err_t err;
    struct response_chunk *r = calloc(1, sizeof(struct response_chunk));
    if (r == NULL) {
        ESP_LOGE(TAG, "malloc failed");
        return;
    }

    esp_http_client_config_t config = {
        .url = URL_TS,
        .method = HTTP_METHOD_GET,
        .event_handler = _http_event_handler,
        .timeout_ms = 10000,
        .user_data = r,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialise HTTP connection");
        free(r);
        return;
    }

    ESP_LOGI(TAG, "Performing HTTP GET: %s", URL_TS);
    err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP GET complete, %d bytes received", (int)r->len);
        parse_weather_json(r->buf);
    } else {
        ESP_LOGE(TAG, "HTTP GET failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    free(r->buf);
    free(r);
}
