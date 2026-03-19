/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <inttypes.h>
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
        if (now > 0) {
            cfgPara.last_timestamp = (uint64_t)now;
            config_save();
        }
        
        Paint_Clear(WHITE);
        // EPD_ShowPicture(0,0,152,152,gImage_cat,BLACK);
        // EPD_ShowChinese(32, 0, (unsigned char*)"温度", 16, BLACK);
        if(cfgPara.standbyMode == 0)//天气预报
        {
            /* 在屏幕顶端中间显示“天气预报”四个字 */
            {
                int text_width = 4 * 16; /* 4个汉字，字号16 */
                int x = (EPD_W > text_width) ? (EPD_W - text_width) / 2 : 0;
                EPD_ShowChinese(x, 0, (unsigned char*)"天气预报", 16, BLACK);
            }
            
        }
        else//纪念日
        {

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
    start_webserver(1);
    
}
