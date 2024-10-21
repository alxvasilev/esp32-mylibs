#include <esp_system.h>
#include <esp_log.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_bt_device.h>
#include <esp_a2dp_api.h>
#include "bluetooth.hpp"
#include <cstring>

BluetoothStack BtStack;
static constexpr BluetoothStack& self() { return BtStack; }

static bool getNameFromEir(uint8_t *eir, char* bdname, uint8_t len);
static bool getUuidFromEir(uint8_t* val, esp_bt_uuid_t& uuid);

void BluetoothStack::gapCallback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_BT_GAP_AUTH_CMPL_EVT: {
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "authentication success: %s", param->auth_cmpl.device_name);
            esp_log_buffer_hex(TAG, param->auth_cmpl.bda, ESP_BD_ADDR_LEN);
        } else {
            ESP_LOGE(TAG, "authentication failed, status:%d", param->auth_cmpl.stat);
        }
        break;
    }
    case ESP_BT_GAP_DISC_RES_EVT: {
        if (self().mDiscovery) {
            self().mDiscovery->addDevice(param);
        }
        break;
    }
    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT: {
        if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
            ESP_LOGI(TAG, "Device discovery stopped");
            auto disco = self().mDiscovery.get();
            if (disco) {
                disco->onComplete(disco->mDevices);
            }
        }
        else if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED) {
            ESP_LOGI(TAG, "Device discovery started");
        }
        break;
    }
    case ESP_BT_GAP_RMT_SRVCS_EVT: {
        if (param->rmt_srvcs.stat == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "Services for device %s found",  bda2str(param->rmt_srvcs.bda).c_str());
            for (int i = 0; i < param->rmt_srvcs.num_uuids; i++) {
                esp_bt_uuid_t *u = param->rmt_srvcs.uuid_list + i;
                ESP_LOGI(TAG, "--%s", uuid2str(*u));
                // ESP_LOGI(GAP_TAG, "--%d", u->len);
            }
        } else {
            ESP_LOGI(TAG, "Services for device %s not found",  bda2str(param->rmt_srvcs.bda).c_str());
        }
        break;
    }
#if (CONFIG_BT_SSP_ENABLED == true)
    case ESP_BT_GAP_CFM_REQ_EVT:
        ESP_LOGI(TAG, "ESP_BT_GAP_CFM_REQ_EVT Please compare the numeric value: %d", param->cfm_req.num_val);
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;
    case ESP_BT_GAP_KEY_NOTIF_EVT:
        ESP_LOGI(TAG, "ESP_BT_GAP_KEY_NOTIF_EVT passkey:%d", param->key_notif.passkey);
        if (self().mShowPinCb) {
            self().mShowPinCb(param->key_notif.passkey);
        }
        break;
    case ESP_BT_GAP_KEY_REQ_EVT:
    #ifdef MYBT_PIN_INPUT
        if (self().mEnterPinCb) {
            ESP_LOGI(TAG, "ESP_BT_GAP_KEY_REQ_EVT Please enter passkey!");
            esp_bt_pin_code_t pin;
            int len = self().mEnterPinCb(pin, sizeof(pin));
            if (len > 0) {
                esp_bt_gap_pin_reply(param->pin_req.bda, true, len, pin);
            }
            else {
                esp_bt_gap_pin_reply(param->pin_req.bda, false, 0, nullptr);
            }
        }
        else {
            ESP_LOGW(TAG, "ESP_BT_GAP_KEY_REQ_EVT Pin requested, but we have no input function");
        }
    #else
        ESP_LOGI(TAG, "ESP_BT_GAP_KEY_REQ_EVT Please enter passkey!");
    #endif
        break;
#endif
    default: {
        ESP_LOGI(TAG, "GAP event: %d", event);
        break;
    }
    }
}
void BluetoothStack::avrcCallback(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param)
{
    ESP_LOGI(TAG, "remote control cb event: %d", event);
}
#if (CONFIG_BT_SSP_ENABLED == true)
#if MYBT_PIN_INPUT
void BluetoothStack::setupPinAuth(ShowPinFunc showPin, EnterPinFunc enterPin)
{
    mShowPinCb = showPin;
    mEnterPinCb = readPin;
    // Classic Bluetooth GAP
    esp_bt_io_cap_t ioCap = showPin
        ? (enterPin ? ESP_BT_IO_CAP_IO : ESP_BT_IO_CAP_OUT)
        : (enterPin ? ESP_BT_IO_CAP_IN : ESP_BT_IO_CAP_NONE);
    esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &ioCap, sizeof(ioCap));
}
#else
void BluetoothStack::setupPinAuth(ShowPinFunc showPin)
{
    // Classic Bluetooth GAP
    mShowPinCb = showPin;
    esp_bt_io_cap_t ioCap = showPin ? ESP_BT_IO_CAP_IN : ESP_BT_IO_CAP_NONE;
    esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &ioCap, sizeof(ioCap));
}

#endif
void BluetoothStack::setPin(uint8_t* pin, int pinLen)
{
    esp_bt_gap_set_pin(ESP_BT_PIN_TYPE_FIXED, pinLen, pin);
}
#endif
bool BluetoothStack::start(esp_bt_mode_t mode, const char* discoName)
{
    if (mMode) {
        ESP_LOGE(TAG, "Already started");
        return false;
    }
#if CONFIG_BT_BLE_DYNAMIC_ENV_MEMORY == TRUE
    hidh_set_dynamic_memory(malloc(sizeof(tHID_HOST_CTB), MALLOC_CAP_SPIRAM));
#endif

//    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));
    esp_err_t err;
    esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    cfg.mode = mode;
    if ((err = esp_bt_controller_init(&cfg)) != ESP_OK) {
        ESP_LOGE(TAG, "%s initialize controller failed: %s\n", __func__, esp_err_to_name(err));
        return false;
    }

    if ((err = esp_bt_controller_enable(mode)) != ESP_OK) {
        ESP_LOGE(TAG, "%s enable controller failed: %s\n", __func__, esp_err_to_name(err));
        return false;
    }

    if ((err = esp_bluedroid_init()) != ESP_OK) {
        ESP_LOGE(TAG, "%s initialize bluedroid failed: %s\n", __func__, esp_err_to_name(err));
        return false;
    }

    if ((err = esp_bluedroid_enable()) != ESP_OK) {
        ESP_LOGE(TAG, "%s enable bluedroid failed: %s\n", __func__, esp_err_to_name(err));
        return false;
    }
    /* set up device name */
    esp_bt_dev_set_device_name(discoName);
    esp_bt_gap_register_callback(gapCallback);

    /* initialize AVRCP controller */
    esp_avrc_ct_init();
    esp_avrc_ct_register_callback(avrcCallback);
    return true;
}
void BluetoothStack::becomeDiscoverableAndConnectable()
{
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
}

void BluetoothStack::disable(esp_bt_mode_t mode)
{
    esp_bluedroid_disable(); // Error: Bluedroid already disabled
    esp_bluedroid_deinit();  // Error: Bluedroid already deinitialized
    esp_bt_controller_disable();
    esp_bt_controller_deinit();
    esp_bt_mem_release(mode);
}
std::string codInfo(esp_bt_cod_t cod, uint32_t codRaw)
{
    std::string result;
    result.resize(32);
    result = "major: ";
    auto maj = esp_hid_cod_major_str(cod.major);
    if (maj) {
        result += maj;
    }
    else {
        result += '(';
        result += std::to_string(cod.major);
        result += ')';
    }
    result += " minor: ";
    auto minor = cod.minor;
    if (cod.major == 5) { // PERIPHERAL
        if (minor & ESP_HID_COD_MIN_KEYBOARD) {
            result += "KEYBOARD";
        }
        if (minor & ESP_HID_COD_MIN_MOUSE) {
            if (minor & ESP_HID_COD_MIN_KEYBOARD) {
                result += '+';
            }
            result += "MOUSE";
        }
        if ((minor & 0xF0) && (minor & 0x0f)) {
            result += "+more(";
            result += std::to_string(minor);
            result += ')';
        }
    }
    else {
        result += '(';
        result += std::to_string(minor);
        result += ')';
    }
    result += " svc: ";
    result += std::to_string(cod.service);
    result += " raw: ";
    result += std::to_string(codRaw);
    return result;
}
bool BluetoothStack::DeviceInfo::isPeripheral(uint8_t type) const {
    return (bt.cod.major == 5) && (bt.cod.minor & type);
}
void BluetoothStack::DeviceInfo::print(const char* tab)
{
    ESP_LOGI(TAG, "%sName: %.*s", tab, name.size(), name.c_str());
    ESP_LOGI(TAG, "%sClass of Device: %s", tab, codInfo(bt.cod, bt.codRaw).c_str());
    ESP_LOGI(TAG, "%sUsage: %s", tab, esp_hid_usage_str(esp_hid_usage_from_cod(bt.codRaw)));
    ESP_LOGI(TAG, "%sRSSI: %d", tab, rssi);
    if (bt.uuid.len) {
        ESP_LOGI(TAG, "%sUUID: %s", tab, uuid2str(bt.uuid));
    }
}

void BluetoothStack::Discovery::addDevice(esp_bt_gap_cb_param_t *param)
{
    Addr addr;
    memcpy(addr.data(), param->disc_res.bda, addr.size());
    if (mDevices.find(addr) != mDevices.end()) {
        return;
    }
    ESP_LOGI(TAG, "Device found: %s", bda2str(param->disc_res.bda).c_str());
    auto& info = mDevices[addr];
    for (int i = 0; i < param->disc_res.num_prop; i++) {
        info.fillFromGapProp(param->disc_res.prop[i]);
    }
    info.print();
}
bool BluetoothStack::DeviceInfo::fillFromGapProp(esp_bt_gap_dev_prop_t& p)
{
    switch (p.type) {
        case ESP_BT_GAP_DEV_PROP_COD:
            bt.codRaw = *(uint32_t *)(p.val);
            return true;
        case ESP_BT_GAP_DEV_PROP_RSSI:
            rssi = *(int8_t *)(p.val);
            return true;
        case ESP_BT_GAP_DEV_PROP_BDNAME:
            if (name.empty()) {
                name.assign((char*)p.val, p.len);
            }
            return true;
        case ESP_BT_GAP_DEV_PROP_EIR: {
            if (getUuidFromEir((uint8_t*)p.val, bt.uuid)) {
                return true;
            }
            char buf[128];
            if (!getNameFromEir((uint8_t*)p.val, buf, sizeof(buf))) {
                ESP_LOGW(TAG, "\tCan't get UUID or name from EIR");
                return false;
            }
            ESP_LOGI(TAG, "\tDevice name from EIR: %s", buf);
            if (name.empty()) {
                name = buf;
            }
            return true;
        }
        default:
            ESP_LOGI(TAG, "Unknown GAP property of type %d", p.type);
            return false;
    }
}
const char* BluetoothStack::bda2str(const uint8_t* bda, char* buf, int bufLen)
{
    if (bda == NULL) {
        return NULL;
    }
    const uint8_t *p = bda;
    snprintf(buf, bufLen, "%02x:%02x:%02x:%02x:%02x:%02x", p[0], p[1], p[2], p[3], p[4], p[5]);
    return buf;
}
std::string BluetoothStack::bda2str(const uint8_t* bda)
{
    std::string result;
    result.resize(18);
    bda2str(bda, &result.front(), 18);
    return result;
}
const char* BluetoothStack::uuid2str(const esp_bt_uuid_t& uuid)
{
    static char str[37];
    if (uuid.len == 2) {
        sprintf(str, "%04x", uuid.uuid.uuid16);
    } else if (uuid.len == 4) {
        sprintf(str, "%08x", uuid.uuid.uuid32);
    } else if (uuid.len == 16) {
        const uint8_t *p = uuid.uuid.uuid128;
        sprintf(str, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                p[15], p[14], p[13], p[12], p[11], p[10], p[9], p[8],
                p[7], p[6], p[5], p[4], p[3], p[2], p[1], p[0]);
    } else {
        return NULL;
    }

    return str;
}

static bool getNameFromEir(uint8_t *eir, char* bdname, uint8_t len)
{
    if (!eir) {
        return false;
    }

    uint8_t rmt_bdname_len = 0;
    auto rmt_bdname = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &rmt_bdname_len);
    if (!rmt_bdname) {
        rmt_bdname = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &rmt_bdname_len);
    }

    if (!rmt_bdname) {
        return false;
    }
    if (rmt_bdname_len > len-1) {
        rmt_bdname_len = len-1;
    }
    memcpy(bdname, rmt_bdname, rmt_bdname_len);
    bdname[rmt_bdname_len] = '\0';
    return true;
}
static bool getUuidFromEir(uint8_t* val, esp_bt_uuid_t& uuid)
{
    uint8_t len = 0;
    uint8_t *data = esp_bt_gap_resolve_eir_data(val, ESP_BT_EIR_TYPE_CMPL_16BITS_UUID, &len);
    if (!data) {
        data = esp_bt_gap_resolve_eir_data(val, ESP_BT_EIR_TYPE_INCMPL_16BITS_UUID, &len);
    }

    if (data && len == ESP_UUID_LEN_16) {
        uuid.len = ESP_UUID_LEN_16;
        uuid.uuid.uuid16 = data[0] + (data[1] << 8);
        return true;
    }
    data = esp_bt_gap_resolve_eir_data(val, ESP_BT_EIR_TYPE_CMPL_32BITS_UUID, &len);
    if (!data) {
        data = esp_bt_gap_resolve_eir_data(val, ESP_BT_EIR_TYPE_INCMPL_32BITS_UUID, &len);
    }
    if (data && len == ESP_UUID_LEN_32) {
        uuid.len = len;
        uuid.uuid.uuid32 = *((uint32_t*)data);
        return true;
    }

    data = esp_bt_gap_resolve_eir_data(val, ESP_BT_EIR_TYPE_CMPL_128BITS_UUID, &len);
    if (!data) {
        data = esp_bt_gap_resolve_eir_data(val, ESP_BT_EIR_TYPE_INCMPL_128BITS_UUID, &len);
    }

    if (data && len == ESP_UUID_LEN_128) {
        uuid.len = len;
        memcpy(uuid.uuid.uuid128, data, len);
        return true;
    }
    return false;
}
