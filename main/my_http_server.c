#include "my_http_server.h"
#include "esp_log.h"  
#include "esp_err.h"

#include "esp_wifi.h"
#include "cJSON.h"

#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "lwip/err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


#include "device_config.h"
//

static const char *TAG = "http_server";
static httpd_handle_t s_server = NULL;
static TaskHandle_t s_dns_task_handle = NULL;
static int s_dns_sock = -1;
static volatile bool s_dns_running = false;

extern SemaphoreHandle_t wifi_connected_semaphore;
#define MAX_AP_LIST_NUM 10

// ======== 嵌入 HTML 文件 ========
extern const uint8_t wifi_config_html_start[] asm("_binary_wifi_config_html_start");
extern const uint8_t wifi_config_html_end[]   asm("_binary_wifi_config_html_end");

// ======== DNS的跳转 ========
// 捕获 Android、iPhone、Windows 等联网检测 URL
static esp_err_t captive_portal_redirect_handler(httpd_req_t *req)
{
    // 返回 HTTP 302 跳转到首页
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, "Redirecting...", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

void register_captive_portal_handlers(httpd_handle_t server)
{
    const httpd_uri_t redirect_uris[] = {
        { .uri = "/generate_204", .method = HTTP_GET, .handler = captive_portal_redirect_handler },
        { .uri = "/gen_204", .method = HTTP_GET, .handler = captive_portal_redirect_handler },
        { .uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = captive_portal_redirect_handler },
        // { .uri = "/ncsi.txt", .method = HTTP_GET, .handler = captive_portal_redirect_handler },
        // { .uri = "/connecttest.txt", .method = HTTP_GET, .handler = captive_portal_redirect_handler },
        // { .uri = "/success.txt", .method = HTTP_GET, .handler = captive_portal_redirect_handler },
        // { .uri = "/favicon.ico", .method = HTTP_GET, .handler = captive_portal_redirect_handler },
    };

    for (int i = 0; i < sizeof(redirect_uris)/sizeof(redirect_uris[0]); i++) {
        httpd_register_uri_handler(server, &redirect_uris[i]);
    }
}

static void dns_server_task(void *pvParameters)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in server, client;
    socklen_t client_len = sizeof(client);
    uint8_t buffer[512];

    if (sock < 0) {
        vTaskDelete(NULL);
        return;
    }

    s_dns_sock = sock;
    s_dns_running = true;

    server.sin_family = AF_INET;
    server.sin_port = htons(53);
    server.sin_addr.s_addr = INADDR_ANY;
    bind(sock, (struct sockaddr*)&server, sizeof(server));

    while (s_dns_running) {
        int len = recvfrom(sock, buffer, sizeof(buffer), 0,
                           (struct sockaddr*)&client, &client_len);
        if (len > 0) {
            // DNS 协议头长 12 字节，简单修改回应
            uint8_t response[512];
            memcpy(response, buffer, len);
            response[2] |= 0x80;   // 设置为响应包
            response[3] |= 0x80;   // 设置为权威回答
            response[7] = 1;       // 回答数量

            int offset = len;
            response[offset++] = 0xC0;
            response[offset++] = 0x0C;
            response[offset++] = 0x00;
            response[offset++] = 0x01;
            response[offset++] = 0x00;
            response[offset++] = 0x01;
            response[offset++] = 0x00;
            response[offset++] = 0x00;
            response[offset++] = 0x00;
            response[offset++] = 0x3C;
            response[offset++] = 0x00;
            response[offset++] = 0x04;

            // 返回 192.168.4.1
            response[offset++] = 192;
            response[offset++] = 168;
            response[offset++] = 4;
            response[offset++] = 1;

            sendto(sock, response, offset, 0,
                   (struct sockaddr*)&client, client_len);
        }
    }

    if (s_dns_sock >= 0) {
        close(s_dns_sock);
        s_dns_sock = -1;
    }
    s_dns_task_handle = NULL;
    vTaskDelete(NULL);
}


esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)wifi_config_html_start,
                    wifi_config_html_end - wifi_config_html_start);
    return ESP_OK;
}


esp_err_t scan_get_handler(httpd_req_t *req)
{

    uint16_t number = MAX_AP_LIST_NUM;
    wifi_ap_record_t ap_info[MAX_AP_LIST_NUM];
    uint16_t ap_count = 0;
    memset(ap_info, 0, sizeof(ap_info));
    esp_wifi_scan_start(NULL, true);
    ESP_LOGI(TAG, "Max AP number ap_info can hold = %u", number);
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&number, ap_info));
    ESP_LOGI(TAG, "Total APs scanned = %u, actual AP number ap_info holds = %u", ap_count, number);
    httpd_resp_set_type(req, "application/json");

    // 返回 JSON 格式的 SSID 列表
    httpd_resp_set_type(req, "application/json");
    char response[512] = "[";  // 静态缓冲区存储响应
    for (int i = 0; i < number; i++) {
        strcat(response, "\"");
        strcat(response, (char *)ap_info[i].ssid);
        strcat(response, "\"");
        if (i < number - 1) strcat(response, ",");
    }
    strcat(response, "]");
    ESP_LOGI(TAG, "Response sent: %s", response);
    httpd_resp_sendstr(req, response);  // 一次性发送
    


    return ESP_OK;
}


esp_err_t connect_post_handler(httpd_req_t *req) {
    // 1. 接收并解析 POST 数据（保留日志方便调试）
    char buf[200];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) {
        ESP_LOGW(TAG, "No POST data received");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[len] = '\0';
    ESP_LOGI(TAG, "Received POST data: %s", buf);
    cJSON *root = cJSON_Parse(buf);
    if (root == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        ESP_LOGE(TAG,"JSON解析错误: %s\n", error_ptr ? error_ptr : "未知错误");
        return ESP_FAIL;
    }
    // 2. 提取ssid字段
    cJSON *ssid_obj = cJSON_GetObjectItemCaseSensitive(root, "ssid");
    if (!cJSON_IsString(ssid_obj) && (ssid_obj->valuestring != NULL)) {
        ESP_LOGE(TAG,"未找到ssid或类型错误\n");
        return ESP_FAIL;
    }
    // 3. 提取password字段
    cJSON *pwd_obj = cJSON_GetObjectItemCaseSensitive(root, "password");
    if (!cJSON_IsString(pwd_obj) && (pwd_obj->valuestring != NULL)) {
        ESP_LOGE(TAG,"未找到password或类型错误\n");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "SSID: %s, PASSWORD: %s", ssid_obj->valuestring, pwd_obj->valuestring);
    esp_wifi_disconnect();
     // WiFi STA配置
    wifi_config_t wifi_config = {0};
    strcpy((char *)wifi_config.sta.ssid, ssid_obj->valuestring);
    strcpy((char *)wifi_config.sta.password, pwd_obj->valuestring);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    esp_wifi_connect();

    //等待5秒，超过5秒视为连接失败
    if (xSemaphoreTake(wifi_connected_semaphore, 5000 / portTICK_PERIOD_MS) == pdTRUE) {
        strcpy(cfgPara.staN, ssid_obj->valuestring);
        strcpy(cfgPara.staP, pwd_obj->valuestring);
        //连接成功，退出配网墨水
        cfgPara.is_wifi_config_mode = 0;
        config_save();
        ESP_LOGI(TAG, "WiFi Connected Successfully, restarting...");
        const char *json_response = "{\"status\":\"ok\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json_response, strlen(json_response));
        cJSON_Delete(root);
        return ESP_OK;
    }
    // 2. 返回 JSON 默认失败状态
    ESP_LOGE(TAG, "WiFi Connection Failed");
    const char *json_response = "{\"status\":\"fail\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_response, strlen(json_response));;

    cJSON_Delete(root);

    return ESP_OK;
}


esp_err_t savesettings_post_handler(httpd_req_t *req)
{
    char buf[64];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) {
        ESP_LOGW(TAG, "No POST data received");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[len] = '\0';
    ESP_LOGI(TAG, "Received POST data: %s", buf);
    cJSON *root = cJSON_Parse(buf);
    if (root == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        ESP_LOGE(TAG,"JSON解析错误: %s\n", error_ptr ? error_ptr : "未知错误");
        return ESP_FAIL;
    }
    // 2. 提取ssid字段
    cJSON *data_obj = cJSON_GetObjectItemCaseSensitive(root, "date");
    if (!cJSON_IsNumber(data_obj)) {
        ESP_LOGE(TAG,"未找到data或类型错误\n");
        return ESP_FAIL;
    }
    cJSON *sleep_mod_obj = cJSON_GetObjectItemCaseSensitive(root, "standbyMode");
    if (!cJSON_IsString(sleep_mod_obj) && (sleep_mod_obj->valuestring != NULL)) {
        ESP_LOGE(TAG,"未找到standbyMode或类型错误\n");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "date: %d", data_obj->valueint);
    ESP_LOGI(TAG, "standbyMode: %s", sleep_mod_obj->valuestring);
    if(memcmp(sleep_mod_obj->valuestring,"anniversary",strlen("anniversary"))==0){
        cfgPara.standbyMode = 1;
    }
    else{
        cfgPara.standbyMode = 0;
    }
    cfgPara.anniversary_data = data_obj->valueint;
    config_save();



    const char *json_response = "{\"status\":\"ok\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_response, strlen(json_response));
    cJSON_free(root);
    return ESP_OK;
}

httpd_handle_t start_webserver(bool dns_enable)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 4096 *2;

    if (s_server != NULL) {
        return s_server;
    }

    if (httpd_start(&s_server, &config) == ESP_OK) {
        httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = root_get_handler };
        httpd_uri_t scan = { .uri = "/scan", .method = HTTP_GET, .handler = scan_get_handler };
        httpd_uri_t connect = { .uri = "/connect", .method = HTTP_POST, .handler = connect_post_handler };
        httpd_uri_t savesettings = { .uri = "/savesettings", .method = HTTP_POST, .handler = savesettings_post_handler };
        httpd_register_uri_handler(s_server, &root);
        httpd_register_uri_handler(s_server, &scan);
        httpd_register_uri_handler(s_server, &connect);
        httpd_register_uri_handler(s_server, &savesettings);
        register_captive_portal_handlers(s_server);
    }

    if(dns_enable && s_dns_task_handle == NULL)
    {
        xTaskCreate(dns_server_task, "dns_server_task", 4096, NULL, 5, &s_dns_task_handle);
    }

    return s_server;
}

void stop_webserver(void)
{
    if (s_server != NULL) {
        httpd_stop(s_server);
        s_server = NULL;
    }

    s_dns_running = false;
    if (s_dns_sock >= 0) {
        shutdown(s_dns_sock, 0);
        close(s_dns_sock);
        s_dns_sock = -1;
    }
}
