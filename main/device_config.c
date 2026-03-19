#include "device_config.h"

#include "string.h"
#include "esp_log.h"


#define TAG "CONFIG"
#define SPI_FLASH_PARA_SECETOR_INDEX 30
#define SPI_FLASH_PARA_ADDR SPI_FLASH_PARA_SECETOR_INDEX*4096
_CONFIG_PARA  cfgPara;

// 读取配置数据
esp_err_t config_read() {
    // 初始化 NVS
    // esp_err_t err = nvs_flash_init();
    // if (err != ESP_OK) {
    //     printf("Error (%s) initializing NVS flash!\n", esp_err_to_name(err));
    //     return;
    // }
    esp_err_t  err;
    // 打开 NVS 命名空间
    nvs_handle_t nvs_handle;

    err = nvs_open("storage", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) return err;


    // 读取配置数据
    // _CONFIG_PARA config;
    size_t required_size;
    err = nvs_get_blob(nvs_handle, "config", NULL, &required_size);
    if (err != ESP_OK || required_size != sizeof(cfgPara)) {
        nvs_close(nvs_handle);
        /* 如果没有有效配置，则恢复默认并保存 */
        config_restore();
        return ESP_OK;
    }

    err = nvs_get_blob(nvs_handle, "config",  (uint32_t *) &cfgPara, &required_size);
    if (err != ESP_OK) {
        printf("Error (%s) reading config data from NVS!\n", esp_err_to_name(err));
    }
      printf("Magic Number: %d\n", cfgPara.magic_num);
    // 关闭 NVS
    nvs_close(nvs_handle);
    return err;
    // 在这里使用 cfg 结构体中的配置参数
}

// 保存配置数据
esp_err_t config_save() {
    // 初始化 NVS
    // 打开 NVS 命名空间
    nvs_handle_t nvs_handle;
    esp_err_t  err;

    err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
        return err;
    }

    // 定义配置参数


    // 写入配置数据
    err = nvs_set_blob(nvs_handle, "config", &cfgPara, sizeof(cfgPara));
    if (err != ESP_OK) {
        printf("Error (%s) writing config data to NVS!\n", esp_err_to_name(err));
    }

    // 提交更改
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    // 关闭 NVS
    nvs_close(nvs_handle);
    return err;
}


void config_restore()
{
	// printf("\r\nRestore parameters");
    ESP_LOGE(TAG,"Restore parameters");
	cfgPara.magic_num = 0x09;
    cfgPara.is_wifi_config_mode = 0;
	strcpy(cfgPara.staN, "");
    strcpy(cfgPara.staP, "");
    cfgPara.anniversary_data = 1643731200; //默认2020年2月2日
    cfgPara.standbyMode = 0;  //0:显示天气 1:显示纪念日
    cfgPara.last_timestamp = 0;
	config_save();

}

uint8_t read_rom_uint8(const uint8_t* addr){
    uint32_t bytes;
    bytes = *(uint32_t*)((uint32_t)addr & ~3);
    return ((uint8_t*)&bytes)[(uint32_t)addr & 3];
}
#if 1
extern const uint8_t  webserver_headPre[];

//const uint8 ICACHE_RODATA_ATTR data[] ICACHE_RODATA_ATTR STORE_ATTR= "HTTP/1.1 200 OK\r\nContent-Type: text/css\r\n\r\n";
void read_rom_flash(char *src, char *des, int len)
{
	//int len = strlen(data);
	//char buf[1025];
	int i=0;
	for(i=0;i <len; i++)
	{
		des[i] = (char)read_rom_uint8((const uint8_t *)(src+i));
	}
	des[len]=0x00;
	//printf("\r\n\r\ntest_flash(len:%d):%s\r\n",len,des);
}
#endif
