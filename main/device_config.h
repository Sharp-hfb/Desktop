/*
 * @Author: hanruo
 * @Date: 2024-04-17 13:45:19
 * @Description: 设置操作，保存在flash中
 * @version:  

 */
/*** 
 * @Descripttion :  
 * @version      :  
 * @Author       : Kevincoooool
 * @Date         : 2021-06-05 10:13:51
 * @LastEditors  : Kevincoooool
 * @LastEditTime : 2021-07-06 14:22:21
 * @FilePath     : \esp-idf\pro\KSDIY_ESPCAM\main\page\page_fft.h
 */


#ifndef __DEVICE_CONFIG__
#define __DEVICE_CONFIG__
#include "nvs_flash.h"
#include "nvs.h"
#include <stdint.h>
#include "driver/uart.h"
#include "esp_wifi.h"

esp_err_t config_read();
esp_err_t config_save();
void config_restore();
// void read_rom_flash(char *src, char*des, int len);
// uint8_t read_rom_uint8(const uint8* addr);
// extern const uint8_t ATTRIBUTES webserver_headPre[];
// void read_rom_flash(char *src, char*des, int len);

void read_rom_flash(char *src, char*des, int len);

typedef struct
{
  uint16_t magic_num;
  uint8_t is_wifi_config_mode; //是否正处于wifi配网模式
  char staN[32];
  char staP[32];
  uint8_t standbyMode;
  int anniversary_data;
  uint64_t last_timestamp; /* unix epoch seconds last saved */

} _CONFIG_PARA;
extern _CONFIG_PARA  cfgPara;

#endif // _TEST_


