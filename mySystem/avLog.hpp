#ifndef AV_LOG_INCLUDED
#define AV_LOG_INCLUDED
#ifdef ESP_PLATFORM
#include <esp_log.h>
    #define AV_LOGI ESP_LOGI
    #define AV_LOGW ESP_LOGW
    #define AV_LOGE ESP_LOGE
    #define AV_LOGD ESP_LOGD
#else
    #define AV_LOG(tag, level, fmt,...) printf("%s %c " fmt "\n", tag, level, ##__VA_ARGS__)
    #define AV_LOGI(tag, fmt,...) AV_LOG(tag, 'I', fmt, ##__VA_ARGS__)
    #define AV_LOGW(tag, fmt,...) AV_LOG(tag, 'W', fmt, ##__VA_ARGS__)
    #define AV_LOGE(tag, fmt,...) AV_LOG(tag, 'E', fmt, ##__VA_ARGS__)
    #define AV_LOGD(tag, fmt,...) AV_LOG(tag, 'D', fmt, ##__VA_ARGS__)
#endif
#endif
