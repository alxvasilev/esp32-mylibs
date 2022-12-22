#include <esp_log.h>
#include <esp_system.h>
#include <esp_http_server.h>
#include <esp_ota_ops.h>
#include <driver/gpio.h>
#include <esp_wifi.h> // for fixes to hanging esp_restart in myRestart()
#include <time.h> // for led blink
#include "ota.hpp"
#include <algorithm>
#ifndef  CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
#error Rollback support is not enabled - please enable it from IDF Bootloader menuconfig, \
option CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
#endif

enum { kOtaBufSize = 512 };
static const char* TAG = "OTA";
static const char* RBK = "ROLLBACK";

void defaultOtaNotifyCallback() {}
volatile bool gOtaInProgress = false;

OtaNotifyCallback otaNotifyCallback = &defaultOtaNotifyCallback;
struct OtaInProgressSetter
{
    OtaInProgressSetter() {
        gOtaInProgress = true;
    }
    ~OtaInProgressSetter() {
        gOtaInProgress = false;
    }
};
void myRestart()
{
    esp_wifi_disconnect();
    vTaskDelay(1);
    esp_wifi_stop();
    vTaskDelay(1);
    esp_wifi_deinit();
    vTaskDelay(1);
    esp_restart();
}

bool rollbackIsPendingVerify()
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t otaState;
    auto err = esp_ota_get_state_partition(running, &otaState);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "Error %s getting active partition state", esp_err_to_name(err));
        return false;
    }
    //ESP_LOGI(TAG, "APP is running from partition %s, state 0x%02X", running->label, otaState);
    return (otaState == ESP_OTA_IMG_PENDING_VERIFY);
}

/* Receive .Bin file */
esp_err_t OTA_update_post_handler(httpd_req_t *req)
{
    OtaInProgressSetter inProgress;
    otaNotifyCallback();
    int contentLen = req->content_len;
    ESP_LOGW(TAG, "OTA request received, image size: %d",contentLen);
    char* otaBuf = new char[kOtaBufSize]; // no need to free it, we will reboot
    const auto update_partition = esp_ota_get_next_update_partition(NULL);
    esp_ota_handle_t ota_handle;
    ESP_LOGW(TAG, "Erasing partition...");
    esp_err_t err = esp_ota_begin(update_partition, contentLen, &ota_handle);
    if (err == ESP_ERR_OTA_ROLLBACK_INVALID_STATE) {
        ESP_LOGW(TAG, "Invalid OTA state of running app, trying to set it");
        esp_ota_mark_app_valid_cancel_rollback();
        err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error %s after attempting to fix OTA state of running app, aborting OTA", esp_err_to_name(err));
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "esp_ota_begin error");
            return ESP_FAIL;
        }
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin returned error %s, aborting OTA", esp_err_to_name(err));
        char msg[64];
        snprintf(msg, 64, "esp_ota_begin() error %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, msg);
        return ESP_FAIL;
    }
    ESP_LOGW(TAG, "Writing to partition '%s' subtype %d at offset 0x%x",
        update_partition->label, update_partition->subtype, update_partition->address);

    uint64_t tsStart = esp_timer_get_time();
    int displayCtr = 0;
    for (int remain = contentLen; remain > 0; )
    {
        /* Read the data for the request */
        int recvLen;
        for (int numWaits = 0; numWaits < 4; numWaits++) {
            recvLen = httpd_req_recv(req, otaBuf, std::min(remain, (int)kOtaBufSize));
            if (recvLen != HTTPD_SOCK_ERR_TIMEOUT) {
                break;
            }
        }
        if (recvLen < 0)
        {
            ESP_LOGE(TAG, "OTA recv error %d, aborting", recvLen);
            return ESP_FAIL;
        }
        remain -= recvLen;
        displayCtr += recvLen;
        if (displayCtr > 10240) {
            displayCtr = 0;
            printf("OTA: Recv %d of %d bytes\r", contentLen - remain, contentLen);
        }
        esp_ota_write(ota_handle, otaBuf, recvLen);
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end error: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "esp_ota_end error");
        return ESP_FAIL;
    }

    // Lets update the partition
    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition error %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "esp_ota_set_boot_partition error");
        return ESP_FAIL;
    }

    const auto bootPartition = esp_ota_get_boot_partition();
    const char msg[] = "OTA update successful";
    httpd_resp_send(req, msg, sizeof(msg));
    ESP_LOGI(TAG, "OTA update successful (%.1f sec)",
        (((double)esp_timer_get_time() - tsStart) / 1000000));
    ESP_LOGI(TAG, "Will boot from partition '%s', subtype %d at offset 0x%x",
        bootPartition->label, bootPartition->subtype, bootPartition->address);

    // Reboot asynchronously, after we return the http response
    ESP_LOGI(TAG, " restarting system...");
    esp_timer_create_args_t args = {};
    args.dispatch_method = ESP_TIMER_TASK;
    args.callback = [](void*) {
        myRestart();
    };
    args.name = "rebootTimer";
    esp_timer_handle_t oneshot_timer;
    ESP_ERROR_CHECK(esp_timer_create(&args, &oneshot_timer));
    ESP_ERROR_CHECK(esp_timer_start_once(oneshot_timer, 1000000));
    return ESP_OK;
}

void rollbackConfirmAppIsWorking()
{
    if (!rollbackIsPendingVerify()) {
        return;
    }
    ESP_LOGW(RBK, "App appears to be working properly, confirming boot partition...");
    esp_ota_mark_app_valid_cancel_rollback();
}

#define ENUM_NAME_CASE(name) case name: return #name

const char* otaPartitionStateToStr(esp_ota_img_states_t state)
{
    switch (state) {
        ENUM_NAME_CASE(ESP_OTA_IMG_NEW);
        ENUM_NAME_CASE(ESP_OTA_IMG_PENDING_VERIFY);
        ENUM_NAME_CASE(ESP_OTA_IMG_VALID);
        ENUM_NAME_CASE(ESP_OTA_IMG_INVALID);
        ENUM_NAME_CASE(ESP_OTA_IMG_ABORTED);
        ENUM_NAME_CASE(ESP_OTA_IMG_UNDEFINED);
        default: return "(UNKNOWN)";
    }
}

bool setOtherPartitionBootableAndRestart()
{
    if (rollbackIsPendingVerify()) {
        esp_ota_mark_app_invalid_rollback_and_reboot();
    }

    ESP_LOGW(RBK, "Could not cancel current OTA, manually switching boot partition");
    auto otherPartition = esp_ota_get_next_update_partition(NULL);
    if (!otherPartition) {
        ESP_LOGE(RBK, "...There is no second OTA partition");
        return false;
    }
    esp_ota_img_states_t state;
    auto err = esp_ota_get_state_partition(otherPartition, &state);
    if (err != ESP_OK) {
        ESP_LOGW(RBK, "...Error getting state of other partition: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(RBK, "Other partition has state %s", otaPartitionStateToStr(state));
    }
    ESP_ERROR_CHECK(esp_ota_set_boot_partition(otherPartition));

    myRestart();
    return true;
}

void blinkLedProgress(gpio_num_t ledPin, int durMs)
{
    durMs *= 1000;
    int64_t end = esp_timer_get_time() + durMs;
    for(;;) {
        gpio_set_level(ledPin, 1);
        vTaskDelay(100 / portTICK_PERIOD_MS);
        gpio_set_level(ledPin, 0);
        int64_t remain = end - esp_timer_get_time();
        if (remain <= 0) {
            return;
        }
        int64_t delay = (remain * 200) / durMs;
        if (delay > remain) {
            delay = remain;
        }
        vTaskDelay(delay / portTICK_PERIOD_MS);
    }
}

bool rollbackCheckUserForced(gpio_num_t rbkPin, gpio_num_t ledPin)
{
    if (gpio_get_level(rbkPin)) {
        return false;
    }
    ESP_LOGW(RBK, "Rollback button press detected, waiting for 4 second to confirm...");
    if (ledPin != -1) {
        blinkLedProgress(ledPin, 4000);
    } else {
        vTaskDelay(4000 / portTICK_PERIOD_MS);
    }
    if (gpio_get_level(rbkPin)) {
        ESP_LOGW(RBK, "Rollback not held for 4 second, rollback canceled");
        return false;
    }
    ESP_LOGW(RBK, "App rollback requested by user button, rolling back and rebooting...");
    setOtherPartitionBootableAndRestart();
    return true;
}
