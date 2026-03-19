/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "esp_system.h"
#include "esp_sleep.h"
#include <sys/time.h>


#include "e_paper.h"
#include "Pic.h"
#include "app_config.h"
#include "my_sht30.h"
#include "deep_sleep.h"
#include "my_button.h"
#include "device_config.h"
#include "my_wifi.h"
#include "my_http_server.h"
#include "system_time.h"

#include "app_getweather.h"
#define TAG "main"


// 全局互斥锁句柄
SemaphoreHandle_t epd_mutex;
float temperature, humidity;

static int utf8_char_count(const char *s)
{
    int count = 0;

    if (s == NULL) {
        return 0;
    }

    while (*s != '\0') {
        if (((unsigned char)*s & 0xC0) != 0x80) {
            count++;
        }
        s++;
    }

    return count;
}

static uint32_t get_elapsed_days_since(time_t start_ts, time_t now_ts)
{
    struct tm start_tm = {0};
    struct tm now_tm = {0};

    localtime_r(&start_ts, &start_tm);
    localtime_r(&now_ts, &now_tm);

    start_tm.tm_hour = 0;
    start_tm.tm_min = 0;
    start_tm.tm_sec = 0;
    start_tm.tm_isdst = -1;
    now_tm.tm_hour = 0;
    now_tm.tm_min = 0;
    now_tm.tm_sec = 0;
    now_tm.tm_isdst = -1;

    time_t start_day = mktime(&start_tm);
    time_t now_day = mktime(&now_tm);

    if (start_day == (time_t)-1 || now_day == (time_t)-1 || now_day <= start_day) {
        return 0;
    }

    return (uint32_t)((now_day - start_day) / 86400);
}


void show_weekday(uint16_t x, uint16_t y,uint8_t weekday)
{
    EPD_ShowChinese(x,y,(unsigned char*)"周",16,BLACK);   
    switch(weekday)
    {
        case 1:
            EPD_ShowChinese(x+16,y,(unsigned char*)"一",16,BLACK);
            break;
        case 2:
            EPD_ShowChinese(x+16,y,(unsigned char*)"二",16,BLACK);
            break;
        case 3:
            EPD_ShowChinese(x+16,y,(unsigned char*)"三",16,BLACK);
            break;
        case 4:
            EPD_ShowChinese(x+16,y,(unsigned char*)"四",16,BLACK);
            break;
        case 5:
            EPD_ShowChinese(x+16,y,(unsigned char*)"五",16,BLACK);
            break;
        case 6:
            EPD_ShowChinese(x+16,y,(unsigned char*)"六",16,BLACK);
            break;
        case 7:
            EPD_ShowChinese(x+16,y,(unsigned char*)"日",16,BLACK);
            break; 
    }
}



void epaper_main_task(void *pvParameter)
{
    uint8_t last_wifi_config_mode = 0xFF;

    EPD_Init();
    EPD_FastMode1Init();
    EPD_Display_Clear();
    EPD_FastUpdate();//更新画面显示
    EPD_Clear_R26H();
    EPD_Display(ImageBW);
    EPD_PartUpdate();
    while(1)
    {
      /*********************局刷模式**********************/
        if (last_wifi_config_mode != cfgPara.is_wifi_config_mode) {
            Paint_Clear(WHITE);
            last_wifi_config_mode = cfgPara.is_wifi_config_mode;
        }

        if (cfgPara.is_wifi_config_mode) {
            if (xSemaphoreTake(epd_mutex, portMAX_DELAY) == pdTRUE) {
                Paint_Clear(WHITE);
                EPD_ShowChinese((EPD_W - 2 * 16) / 2 - 4, (EPD_H - 16) / 2 - 4, (unsigned char*)"设置中", 16, BLACK);
                EPD_Display(ImageBW);
                EPD_PartUpdate();
                xSemaphoreGive(epd_mutex);
            }
            vTaskDelay(500/portTICK_PERIOD_MS);
            continue;
        }
        
        struct timeval tv_now;
        gettimeofday(&tv_now, NULL);
        struct tm tm_now;
        localtime_r(&tv_now.tv_sec, &tm_now);
        
        float time_val = tm_now.tm_hour + tm_now.tm_min / 100.0;
        u16 time_sec = tv_now.tv_sec % 60;
        

        my_sht30_get_data(&temperature, &humidity);
        //这边加锁是防止要进入睡眠，睡眠回调的换图跟这边的刷新冲突
        if (xSemaphoreTake(epd_mutex, portMAX_DELAY) == pdTRUE) {
            //显示传感器数据
            EPD_ShowPicture(0,88,32,32,gImage_temp,BLACK);
            EPD_ShowPicture(0,120,32,32,gImage_himi,BLACK);
            EPD_ShowSensor_Data(32,90,temperature,4,2,24,BLACK);
            EPD_ShowSensor_Data(32,122,humidity,4,2,24,BLACK);
            //显示时间
            EPD_ShowWatch(12,20,time_val,4,2,48,BLACK);
            EPD_ShowNum_Two(135,48,time_sec,12,BLACK);
            //显示日期
            EPD_ShowNum_Two(15, 5,(uint16_t)tm_now.tm_mon+1, 16, BLACK);
            EPD_ShowChinese(15+16+1,5,(unsigned char*)"月",16,BLACK);            
            EPD_ShowNum_Two(15+16+16+5, 5, tm_now.tm_mday, 16, BLACK);
            EPD_ShowChinese(15+16+16+16+5, 5, (unsigned char*)"日", 16, BLACK);
            show_weekday(15+16+16+16+16+10, 5, tm_now.tm_wday);
            //小人物根据温度显示表情状态
            if(temperature>30.0){
                EPD_ShowPicture(97,72,53,80,gImage_hot,BLACK);
            }else if(temperature<20.0){
                EPD_ShowPicture(97,72,53,80,gImage_cold,BLACK);
            }else{
                EPD_ShowPicture(97,72,53,80,gImage_comfor,BLACK);
            }
            //显示wifi连接状态
            if(g_wifi_is_connected){
                EPD_ShowPicture(125,0,24,24,gImage_wifi,BLACK);
            }
            else{
                EPD_ShowPicture(125,0,24,24,gImage_null,BLACK);
            }

            EPD_Display(ImageBW);
            EPD_PartUpdate();
            xSemaphoreGive(epd_mutex); // 释放锁
        }
        vTaskDelay(500/portTICK_PERIOD_MS);
    }
}

/// @brief 执行完回调函数才会进入睡眠
/// @param  
void enter_deep_sleep_cb(void)
{
    if (xSemaphoreTake(epd_mutex, portMAX_DELAY) == pdTRUE) {
        /* 保存当前时间到 flash，供上电后恢复时间使用 */
        
        time_t now = time(NULL);
        struct tm tm_now = {0};
        localtime_r(&now, &tm_now);
        if (now > 0) {
            cfgPara.last_timestamp = (uint64_t)now;
            config_save();
        }
        
        Paint_Clear(WHITE);
        // EPD_ShowPicture(0,0,152,152,gImage_cat,BLACK);
        // EPD_ShowChinese(32, 0, (unsigned char*)"温度", 16, BLACK);
        if(cfgPara.standbyMode == 0)//天气预报
        {
            const char *wind_text = (g_weather_response.forecast.fx[0] != '\0') ? g_weather_response.forecast.fx : "暂无";
            const char *weather_text = (g_weather_response.forecast.type[0] != '\0') ? g_weather_response.forecast.type : "未知";
            const char *high_text = (g_weather_response.forecast.high[0] != '\0') ? g_weather_response.forecast.high : "--";
            const char *low_text = (g_weather_response.forecast.low[0] != '\0') ? g_weather_response.forecast.low : "--";
            const char *humidity_text = (g_weather_response.forecast.humidity[0] != '\0') ? g_weather_response.forecast.humidity : "--";
            /* 在屏幕顶端中间显示“天气预报”四个字 */
            {
                int text_width = 4 * 16; /* 4个汉字，字号16 */
                int x = (EPD_W > text_width) ? (EPD_W - text_width) / 2 : 0;
                EPD_ShowChinese(x, 0, (unsigned char*)"天气预报", 16, BLACK);
            }

            /* 在天气预报下方居中显示今天的日期 */
            {
                int month = tm_now.tm_mon + 1;
                int day = tm_now.tm_mday;
                int month_digits = (month >= 10) ? 2 : 1;
                int day_digits = (day >= 10) ? 2 : 1;
                int text_width = (month_digits + day_digits + 2) * 8; /* 数字8像素宽，月/日各16像素宽 */
                int x = (EPD_W > text_width) ? (EPD_W - text_width) / 2 : 0;
                int y = 24;

                EPD_ShowNum(x-6, y, month, month_digits, 16, BLACK);
                x += month_digits * 8;
                EPD_ShowChinese(x-6, y, (unsigned char*)"月", 16, BLACK);
                x += 16;
                EPD_ShowNum(x-6, y, day, day_digits, 16, BLACK);
                x += day_digits * 8;
                EPD_ShowChinese(x-6, y, (unsigned char*)"日", 16, BLACK);
            }

            /* 日期下方一行显示风向图标和文字 */
            {
                int icon_x = 0;
                int icon_y = 48;
                int text_x = 36;
                int text_y = 56;
                int weather_icon_x = text_x + utf8_char_count(wind_text) * 16 + 4;
                int weather_text_x = weather_icon_x + 40;

                EPD_ShowPicture(icon_x, icon_y, 32, 32, gImage_wind, BLACK);
                EPD_ShowChinese(text_x, text_y, (unsigned char *)wind_text, 16, BLACK);
                EPD_ShowPicture(weather_icon_x, icon_y, 32, 32, gImage_weather, BLACK);
                EPD_ShowChinese(weather_text_x, text_y, (unsigned char *)weather_text, 16, BLACK);
                
            }

            /* 再下一行显示最高温和最低温 */
            {
                int icon_y = 85;
                int text_y = 96;
                int high_icon_x = 0;
                int high_text_x = 36;
                int low_icon_x = 76;
                int low_text_x = 112;

                EPD_ShowPicture(high_icon_x, icon_y, 32, 32, gImage_temp_up, BLACK);
                EPD_ShowString(high_text_x, text_y, (uint8_t *)high_text, 16, BLACK);
                EPD_ShowPicture(low_icon_x, icon_y, 32, 32, gImage_temp_down, BLACK);
                EPD_ShowString(low_text_x, text_y, (uint8_t *)low_text, 16, BLACK);
            }

            /* 最后一行显示湿度 */
            {
                int icon_x = 0;
                int icon_y = 115;
                int text_y = 120;
                char humidity_display[20];

                snprintf(humidity_display, sizeof(humidity_display), "%s%%", humidity_text);
                EPD_ShowPicture(icon_x, icon_y, 32, 32, gImage_himi, BLACK);
                EPD_ShowString(36, text_y, (uint8_t *)humidity_display, 16, BLACK);
            }
            
        }
        else//纪念日
        {
            uint32_t elapsed_days = get_elapsed_days_since((time_t)cfgPara.anniversary_data, now);
            struct tm anniversary_tm = {0};
            time_t anniversary_ts = (time_t)cfgPara.anniversary_data;
            char days_buf[12];
            int digits = snprintf(days_buf, sizeof(days_buf), "%lu", (unsigned long)elapsed_days);
            int year_text_x = (EPD_W - 112) / 2;
            int icon_x = (EPD_W - 64) / 2;
            int icon_y = 16;
            int text_y = icon_y + 64 + 12;
            int date_y = text_y + 22;
            int text_width = 16 + digits * 8 + 16;
            int text_x = (EPD_W > text_width) ? (EPD_W - text_width) / 2 : 0;

            localtime_r(&anniversary_ts, &anniversary_tm);

            EPD_ShowPicture(icon_x, icon_y, 64, 64, gImage_anniversary, BLACK);
            EPD_ShowChinese(text_x, text_y, (unsigned char *)"第", 16, BLACK);
            EPD_ShowString(text_x + 16, text_y, (uint8_t *)days_buf, 16, BLACK);
            EPD_ShowChinese(text_x + 16 + digits * 8, text_y, (unsigned char *)"天", 16, BLACK);
            // printf("anniversary_tm.tm_year = %d\n", anniversary_tm.tm_year);
            // printf("anniversary_tm.tm_mon = %d\n", anniversary_tm.tm_mon);
            // printf("anniversary_tm.tm_mday = %d\n", anniversary_tm.tm_mday);
            EPD_ShowNum(year_text_x, date_y, anniversary_tm.tm_year + 1900, 4, 16, BLACK);
            EPD_ShowChinese(year_text_x + 32, date_y, (unsigned char *)"年", 16, BLACK);
            EPD_ShowNum(year_text_x + 48, date_y, anniversary_tm.tm_mon + 1, 2, 16, BLACK);
            EPD_ShowChinese(year_text_x + 64, date_y, (unsigned char *)"月", 16, BLACK);
            EPD_ShowNum(year_text_x + 80, date_y, anniversary_tm.tm_mday, 2, 16, BLACK);
            EPD_ShowChinese(year_text_x + 96, date_y, (unsigned char *)"日", 16, BLACK);
        }
        EPD_Display(ImageBW);
        EPD_PartUpdate();
        EPD_DeepSleep();
        xSemaphoreGive(epd_mutex); // 释放锁
    }
}




void app_main(void)
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    config_read();
    printf_weakeup_reason();
    /* 如果是上电/重启（非深度睡眠唤醒），则从 flash 读取并恢复时间 */
    {
        esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
        if (wakeup_reason == ESP_SLEEP_WAKEUP_UNDEFINED) {
            if (cfgPara.last_timestamp != 0) {
                set_system_time((time_t)cfgPara.last_timestamp);
                ESP_LOGI(TAG, "Restored time from NVS: %llu", (unsigned long long)cfgPara.last_timestamp);
            }
        }
    }
    set_system_time_country();
    // set_system_time(1761128634);
    my_button_init();
    my_sht30_init();
    epaper_spi_init();
    // 创建互斥锁
    epd_mutex = xSemaphoreCreateMutex();
    if (epd_mutex == NULL) {
        ESP_LOGE(TAG, "互斥锁创建失败!\n");
        return;
    }
    xTaskCreate(epaper_main_task, "epaper_main_task", 2048*2, NULL, 10, NULL);
    
    ESP_ERROR_CHECK(create_deep_sleep_timer(ENTER_DEEP_SLEEP_TIME));
    register_deep_sleep_callback(enter_deep_sleep_cb);

    //wifi中会调用定时器相关函数，必须在定时器初始化过后再初始化wifi
    wifi_init();
    if (cfgPara.is_wifi_config_mode) {
        start_webserver(1);
    }
    
}
