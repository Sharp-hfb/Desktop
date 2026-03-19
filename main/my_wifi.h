#ifndef MY_WIFI_H
#define MY_WIFI_H

#include "esp_system.h"
extern bool g_wifi_is_got_ip,g_wifi_is_connected;
void wifi_init(void);
void enter_wifi_config_mode_reset();
void exit_wifi_config_mode(void);
bool wifi_is_got_ip();

#endif // MY_WIFI_H
