#ifndef _APP_GETWEATHER_H_
#define _APP_GETWEATHER_H_
// 天气数据结构体
typedef struct {
    char date[8];           // 日期
    char high[32];          // 最高温度
    char low[32];           // 最低温度
    char humidity[16];      // 湿度
    char ymd[16];           // 年月日
    char week[16];          // 星期
    char sunrise[16];       // 日出时间
    char sunset[16];        // 日落时间
    int aqi;                // 空气质量指数
    char fx[16];            // 风向
    char fl[16];            // 风力
    char type[24];          // 天气类型
    char notice[64];        // 提示信息
} WeatherData;

// 完整的响应结构体
typedef struct {
    int status;
    char date[16];
    char time[24];
    WeatherData forecast;   // 只存储第一条预报数据
} WeatherResponse;

extern WeatherResponse g_weather_response;

void get_network_weather();

#endif /* _APP_GETWEATHER_H_ */
