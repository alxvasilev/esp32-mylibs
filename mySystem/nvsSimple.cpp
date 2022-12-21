#include "nvsSimple.hpp"
#include <nvs_flash.h>
#include <esp_log.h>
#include <httpServer.hpp>
#include "buffer.hpp"

esp_err_t NvsSimple::init(bool eraseOnError)
{
    bool freshNew = false;
    /* Initialize NVS — it is used to store PHY calibration data */
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        if (eraseOnError && err != ESP_ERR_NOT_FOUND && err != ESP_ERR_NO_MEM) {
            ESP_LOGE("NVS", "nvs_flash_init() returned error %s, erasing NVS and retrying", esp_err_to_name(err));
            ESP_ERROR_CHECK(nvs_flash_erase());
            ESP_ERROR_CHECK(nvs_flash_init());
            freshNew = true;
        } else {
            ESP_LOGE("NVS", "nvs_flash_init() returned error %s, bailing out", esp_err_to_name(err));
            return err;
        }
    }
    err = nvs_open_from_partition("nvs", "global", NVS_READWRITE, &mHandle);
    if (err == ESP_OK) {
        return ESP_OK;
    }
    mHandle = 0;
    if (freshNew || !eraseOnError) {
        return err;
    }
    ESP_LOGE("NVS", "Error opening NVS handle: %s, erasing NVS and retrying", esp_err_to_name(err));
    ESP_ERROR_CHECK(nvs_flash_erase());
    ESP_ERROR_CHECK(nvs_flash_init());
    err = nvs_open_from_partition("nvs", "global", NVS_READWRITE, &mHandle);
    if (err == ESP_OK) {
        return ESP_OK;
    }
    ESP_LOGE("NVS", "Error opening NVS handle: %s after fresh format", esp_err_to_name(err));
    if (eraseOnError) {
        ESP_ERROR_CHECK(err);
        return err; // just to prevent compiler warning
    } else {
        mHandle = 0;
        return err;
    }
}
BufPtr<char> NvsSimple::getString(const char* key)
{
    size_t size = 0;
    auto err = nvs_get_str(mHandle, key, nullptr, &size);
    if (err) {
        return nullptr;
    }
    char* str = (char*)malloc(size); // size incudes the null terminator
    assert(str);
    err = nvs_get_str(mHandle, key, str, &size);
    return BufPtr<char>(err == ESP_OK ? str : nullptr);
}

int32_t NvsSimple::getInt32(const char* key, int32_t defVal)
{
    int32_t result;
    auto err = nvs_get_i32(mHandle, key, &result);
    return (err == ESP_OK) ? result : defVal;
}

NvsSimple::~NvsSimple()
{
    if (mHandle) {
        nvs_close(mHandle);
        mHandle = 0;
    }
}
esp_err_t NvsSimple::httpDumpToJson(httpd_req_t* req)
{
    auto& self = *static_cast<NvsSimple*>(req->user_ctx);
    std::string json = "{";
    nvs_iterator_t it = nvs_entry_find("nvs", "global", NVS_TYPE_ANY);
    for(; it; it = nvs_entry_next(it)) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        if (info.type == NVS_TYPE_STR) {
            BufPtr<char> val(self.getString(info.key));
            json += '\"';
            json.append(info.key).append("\":\"").append(val.ptr()) += '\"';
        } else if (info.type == NVS_TYPE_I32) {
            json += '\"';
            json.append(info.key).append("\":");
            appendAny(json, self.getInt32(info.key, 0));
        }
        httpd_resp_send_chunk(req, json.c_str(), json.size());
        json = ",";
    }
    httpd_resp_sendstr_chunk(req, "}");
    httpd_resp_sendstr_chunk(req, nullptr);
    return ESP_OK;
}
esp_err_t NvsSimple::httpSetParam(httpd_req_t* req)
{
    auto& self = *static_cast<NvsSimple*>(req->user_ctx);
    UrlParams params(req);
    for (auto& keyVal: params.keyVals()) {
        const auto& key = keyVal.key;
        if (keyVal.val.str[0] == 0) { // empty value, delete
            if (key.len > 3) { // key is not required to have :type suffix, but it may
                char* colon = key.str + key.len - 3;
                if (*colon == ':') { // has semicolon
                    *colon = 0;
                }
            } else if (key.len < 2) {
                jsonSendError(req, "Key name '", key.str, "' for deleted value is too short");
                return ESP_FAIL;
            }
            nvs_erase_key(self.handle(), key.str);
            ESP_LOGI("NVS", "Delete param '%s'", key.str);
            continue;
        }
        // non-empty val, set it
        if (key.len < 4) {
            jsonSendError(req, "Key name '", key.str, "' too short");
            return ESP_FAIL;
        }
        char* colon = key.str + key.len - 3;
        if (*colon != ':') {
            jsonSendError(req, "Key name '", key.str, "' has no :type suffix");
            return ESP_FAIL;
        }
        *colon = 0; // null-terminate before the colon
        char type = *(colon + 1);
        esp_err_t err;
        if (type == 's') {
            if (self.strValValidator && !self.strValValidator(key, keyVal.val)) {
                jsonSendError(req, "Validation failed for string key '", key.str, "'");
                return ESP_FAIL;
            }
            err = nvs_set_str(self.handle(), key.str, keyVal.val.str);
        } else if (type == 'i') {
            errno = 0;
            char* next = nullptr;
            const char* val = keyVal.val.str;
            auto nVal = strtol(val, &next, 10);
            if (next == val || errno != 0) {
                jsonSendError(req, "Value for key '", key.str, "' is not a vaild number");
                return ESP_FAIL;
            }
            if (self.int32ValValidator && !self.int32ValValidator(key, nVal)) {
                jsonSendError(req, "Validation failed for numeric key '", key.str, "'");
                return ESP_FAIL;
            }
            err = nvs_set_i32(self.handle(), key.str, nVal);
        }
        ESP_LOGI("NVS", "Set param '%s' type %c to '%s': %s", key.str, type, keyVal.val.str, esp_err_to_name(err));
        if (err != ESP_OK) {
            jsonSendError(req, "Error setting value '", key.str, "' type ", type, ": ", esp_err_to_name(err));
            return ESP_FAIL;
        }
    }
    nvs_commit(self.mHandle);
    jsonSendOk(req);
    return ESP_OK;
}
esp_err_t NvsSimple::httpDelParam(httpd_req_t* req)
{
    auto& self = *static_cast<NvsSimple*>(req->user_ctx);
    UrlParams params(req);
    auto key = params.strVal("key");
    if (!key.str) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'key' URL param");
        return ESP_FAIL;
    }
    auto err = nvs_erase_key(self.handle(), key.str);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, esp_err_to_name(err));
        return ESP_FAIL;
    }
    nvs_commit(self.mHandle);
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}
void NvsSimple::registerHttpHandlers(http::Server& server)
{
    server.on("/nvdump", HTTP_GET, NvsSimple::httpDumpToJson, this);
    server.on("/nvset", HTTP_GET, NvsSimple::httpSetParam, this);
    server.on("/nvdel", HTTP_GET, NvsSimple::httpDelParam, this);
}

