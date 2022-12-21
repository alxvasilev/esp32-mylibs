#ifndef OTA_HPP_INCLUDED
#define OTA_HPP_INCLUDED

#include <esp_ota_ops.h>

bool rollbackIsPendingVerify();
void rollbackConfirmAppIsWorking();
bool rollbackCheckUserForced(gpio_num_t rbkPin, gpio_num_t ledPin);

const char* otaPartitionStateToStr(esp_ota_img_states_t state);
bool setOtherPartitionBootableAndRestart();
// Web server request handler to register with the applcation's server
esp_err_t OTA_update_post_handler(httpd_req_t *req);

// This callback is called just before OTA starts. The application can
// use it to stop any ongoing process
typedef void(*OtaNotifyCallback)();
extern OtaNotifyCallback otaNotifyCallback;
extern bool otaInProgress;
#endif
