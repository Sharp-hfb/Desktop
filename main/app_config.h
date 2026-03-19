
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#define WAKEUP_PIN                  GPIO_NUM_0   // 使用IO0为唤醒引脚
#define ENTER_DEEP_SLEEP_TIME       10 * 1000          // 10秒，单位：ms
#define RTC_WAKEUP_TIME             1800 * 1000 * 1000  // 30分钟，单位：us
#define WIFI_CONFIG_TIMEOUT        5 * 60 * 1000      // 5分钟，单位：ms
//  #define WIFI_CONFIG_TIMEOUT 10 * 1000 

/* SHT30 I2C 引脚配置 */
#define I2C_MASTER_SCL_IO           GPIO_NUM_32              /*!< gpio number for I2C master clock */
#define I2C_MASTER_SDA_IO           GPIO_NUM_33             /*!< gpio number for I2C master data  */
#define I2C_MASTER_NUM              I2C_NUM_0              /*!< I2C port number for master dev */
#define I2C_MASTER_FREQ_HZ          100000                 /*!< I2C master clock frequency */

/* 按键有效电平配置 */
#define BUTTON_ACTIVE_LEVEL         0
/* 按键引脚配置 */
#define MAIN_BUTTON_PIN             0
#define UP_BUTTON_PIN               19
#define DOWN_BUTTON_PIN             23
#define MID_BUTTON_PIN              22

#endif /* APP_CONFIG_H */