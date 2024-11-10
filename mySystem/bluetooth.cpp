#include <esp_system.h>
#include <esp_log.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_bt_device.h>
#include <esp_a2dp_api.h>
#include "bluetooth.hpp"
#include <cstring>
#include "magic_enum.hpp"
#include "utils.hpp"

BluetoothStack BtStack;
static constexpr BluetoothStack& self() { return BtStack; }

static bool getNameFromEir(uint8_t *eir, char* bdname, uint8_t len);
static bool getUuidFromEir(uint8_t* val, esp_bt_uuid_t& uuid);
static const char* gapEventToStr(esp_bt_gap_cb_event_t event);
const char* bleAddrTypeToStr(esp_ble_addr_type_t type);
const char* bleKeyTypeToStr(esp_ble_key_type_t key_type);

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
            self().mDiscovery->addDevice(param->disc_res);
        }
        break;
    }
    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT: {
        if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
            ESP_LOGI(TAG, "Device discovery stopped");
            auto disco = self().mDiscovery.get();
            if (disco) {
                disco->onComplete(disco->devices);
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
#endif // SSP enabled
    default: {
        ESP_LOGI(TAG, "Unhandled GAP event: %s(%d)", gapEventToStr(event), event);
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

#ifdef CONFIG_BT_BLE_ENABLED
void BluetoothStack::bleGapCallback(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t * param)
{
  switch (event) {
    // SCAN
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT: {
      break;
    }
    case ESP_GAP_BLE_SCAN_RESULT_EVT: {
      auto srchEvt = param->scan_rst.search_evt;
      if (srchEvt == ESP_GAP_SEARCH_INQ_RES_EVT) {
          if (self().mDiscovery) {
              self().mDiscovery->addDevice(param->scan_rst);
          }
          else if(srchEvt == ESP_GAP_SEARCH_INQ_CMPL_EVT) {
              ESP_LOGV(TAG, "BLE GAP EVENT SCAN DONE: %d", param->scan_rst.num_resps);
          }
      }
      break;
    }
    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT: {
        ESP_LOGV(TAG, "BLE GAP EVENT SCAN CANCELED");
        break;
    }
    // ADVERTISEMENT
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        ESP_LOGV(TAG, "BLE GAP ADV_DATA_SET_COMPLETE");
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        ESP_LOGV(TAG, "BLE GAP ADV_START_COMPLETE");
        break;
    // AUTHENTICATION
    case ESP_GAP_BLE_AUTH_CMPL_EVT:
        if (!param->ble_security.auth_cmpl.success) {
            ESP_LOGE(TAG, "BLE GAP AUTH ERROR: 0x%x", param->ble_security.auth_cmpl.fail_reason);
        }
        else {
            ESP_LOGV(TAG, "BLE GAP AUTH SUCCESS");
        }
        break;
    case ESP_GAP_BLE_KEY_EVT: //shows the ble key info share with peer device to the user.
        ESP_LOGV(TAG, "BLE GAP KEY type = %s", bleKeyTypeToStr(param->ble_security.ble_key.key_type));
        break;
    case ESP_GAP_BLE_PASSKEY_NOTIF_EVT: // ESP_IO_CAP_OUT
        // The app will receive this evt when the IO has Output capability and the peer device IO has Input capability.
        // Show the passkey number to the user to input it in the peer device.
        ESP_LOGV(TAG, "BLE GAP PASSKEY_NOTIF passkey: %lu", param->ble_security.key_notif.passkey);
#if CONFIG_BT_SSP_ENABLED
        if (self().mShowPinCb) {
            self().mShowPinCb(param->ble_security.key_notif.passkey);
        }
#endif
        break;
    case ESP_GAP_BLE_NC_REQ_EVT: // ESP_IO_CAP_IO
      // The app will receive this event when the IO has DisplayYesNO capability
      // and the peer device IO also has DisplayYesNo capability.
      // Show the passkey number to the user to confirm it with the number displayed by peer device.
      ESP_LOGV(TAG, "BLE GAP NC_REQ passkey: %lu", param->ble_security.key_notif.passkey);
      esp_ble_confirm_reply(param->ble_security.key_notif.bd_addr, true);
      break;
    case ESP_GAP_BLE_PASSKEY_REQ_EVT: // ESP_IO_CAP_IN
      // The app will receive this evt when the IO has Input capability and the peer device IO has Output capability.
      // See the passkey number on the peer device and send it back.
      ESP_LOGV(TAG, "BLE GAP PASSKEY_REQ");
#if CONFIG_BT_SSP_ENABLED
      if (self().mEnterPinCb) {
          ESP_LOGI(TAG, "ESP_GAP_BLE_PASSKEY_REQ_EVT: Please enter passkey");
          esp_bt_pin_code_t pin;
          int pin = self().mEnterPinCb(nullptr, 0);
          if (pin > 0) {
              esp_ble_passkey_reply(param->ble_security.ble_req.bd_addr, true, pin);
          }
          else {
              esp_ble_passkey_reply(param->ble_security.ble_req.bd_addr, false, 0);
          }
      }
#endif
      break;
    case ESP_GAP_BLE_SEC_REQ_EVT:
        ESP_LOGV(TAG, "BLE GAP SEC_REQ");
        // Send the positive(true) security response to the peer device to accept the security request.
        // If not accept the security request, should send the security response with negative(false) accept value.
        esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
        break;
    default:
        ESP_LOGV(TAG, "BLE GAP EVENT %s", magic_enum::enum_name(event).data());
        break;
    }
}

BluetoothStack::DeviceInfo::DeviceInfo(esp_ble_gap_cb_param_t::ble_scan_result_evt_param& info)
: isBle(true)
{
  memcpy(addr, info.bda, sizeof(addr));
  auto advData = info.ble_adv;
  uint8_t len = 0;
  uint8_t* uuid_d = esp_ble_resolve_adv_data(advData, ESP_BLE_AD_TYPE_16SRV_CMPL, &len);
  if (uuid_d && len) {
      ble.uuid = uuid_d[0] + (uuid_d[1] << 8);
  }
  else {
      ble.uuid = 0;
  }

  len = 0;
  uint8_t* appearance_d = esp_ble_resolve_adv_data(advData, ESP_BLE_AD_TYPE_APPEARANCE, &len);
  if (appearance_d && len) {
    ble.appearance = appearance_d[0] + (appearance_d[1] << 8);
  }
  else {
      ble.appearance = 0;
  }

  ble.addrType = info.ble_addr_type;
  rssi = info.rssi;
  len = 0;
  uint8_t* adv_name = esp_ble_resolve_adv_data(advData, ESP_BLE_AD_TYPE_NAME_CMPL, &len);
  if (!adv_name) {
      adv_name = esp_ble_resolve_adv_data(advData, ESP_BLE_AD_TYPE_NAME_SHORT, &len);
  }
  if (adv_name && len) {
      name.assign((const char*)adv_name, len);
  }
}
#endif
bool BluetoothStack::start(esp_bt_mode_t mode, const char* discoName)
{
    if (mMode) {
        ESP_LOGE(TAG, "Already started");
        return true;
    }
    esp_err_t err;
    esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    cfg.mode = mode;
    cfg.ble_max_conn = 3;
    cfg.bt_max_acl_conn = 3;
    if ((err = esp_bt_controller_init(&cfg)) != ESP_OK) {
        ESP_LOGE(TAG, "esp_bt_controller_init failed: %s\n", esp_err_to_name(err));
        shutdown();
        return false;
    }

    if ((err = esp_bt_controller_enable(mode)) != ESP_OK) {
        ESP_LOGE(TAG, "esp_bt_controller_enable failed: %s\n", esp_err_to_name(err));
        shutdown();
        return false;
    }

    if ((err = esp_bluedroid_init()) != ESP_OK) {
        ESP_LOGE(TAG, "esp_bluedroid_init failed: %s\n", esp_err_to_name(err));
        shutdown();
        return false;
    }

    if ((err = esp_bluedroid_enable()) != ESP_OK) {
        ESP_LOGE(TAG, "esp_bluedroid_enable failed: %s\n", esp_err_to_name(err));
        shutdown();
        return false;
    }
    /* set up device name */
    esp_bt_gap_set_device_name(discoName);
    if (mode & ESP_BT_MODE_CLASSIC_BT) {
        MY_ESP_ERRCHECK(esp_bt_gap_register_callback(gapCallback), TAG, "reg bt gap callback",);
    }
    if (mode & ESP_BT_MODE_BLE) {
        MY_ESP_ERRCHECK(esp_ble_gap_register_callback(bleGapCallback), TAG, "reg ble gap callback",);
    }

    /* initialize AVRCP controller */
    esp_avrc_ct_init();
    esp_avrc_ct_register_callback(avrcCallback);
    mMode = mode;
    return true;
}
bool BluetoothStack::doDiscoverDevices(float secs, Discovery* disco)
{
    myassert(!mDiscovery);
    mDiscovery.reset(disco);
    bool ok = true;
    auto mode = disco->mode;
    if (mode & ESP_BT_MODE_CLASSIC_BT) {
        MY_ESP_ERRCHECK(esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, int(secs / 1.28 + .5), 0),
          "bt", "starting classic scan", ok = false);
    }
#if CONFIG_BT_BLE_ENABLED
    static esp_ble_scan_params_t bleScanParams = {
        .scan_type              = BLE_SCAN_TYPE_ACTIVE,
        .own_addr_type          = BLE_ADDR_TYPE_PUBLIC,
        .scan_filter_policy     = BLE_SCAN_FILTER_ALLOW_ALL,
        .scan_interval          = 0x50,
        .scan_window            = 0x30,
        .scan_duplicate         = BLE_SCAN_DUPLICATE_ENABLE
    };
    if (mode & ESP_BT_MODE_BLE) {
        MY_ESP_ERRCHECK(esp_ble_gap_set_scan_params(&bleScanParams), "bt", "setting BLE scan params", ok = false);
        MY_ESP_ERRCHECK(esp_ble_gap_start_scanning(secs), "bt", "starting BLE scan", ok = false);
    }
#endif
    return ok;
}
esp_err_t BluetoothStack::becomeDiscoverableAndConnectable()
{
    return esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
}
void BluetoothStack::stopDiscovery()
{
    if (!mDiscovery) {
        return;
    }
    auto mode = mDiscovery->mode;
    if (mode & ESP_BT_MODE_CLASSIC_BT) {
        MY_ESP_ERRCHECK(esp_bt_gap_cancel_discovery(), TAG, "stopping classic discovery",);
    }
    if (mode & ESP_BT_MODE_BLE) {
        MY_ESP_ERRCHECK(esp_ble_gap_stop_scanning(), TAG, "stopping classic discovery",);
    }
    mDiscovery->mode = ESP_BT_MODE_IDLE;
}
void BluetoothStack::shutdown()
{
    esp_bluedroid_disable(); // Error: Bluedroid already disabled
    esp_bluedroid_deinit();  // Error: Bluedroid already deinitialized
    esp_bt_controller_disable();
    esp_bt_controller_deinit();
}
void BluetoothStack::disable(esp_bt_mode_t mode)
{
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
void BluetoothStack::DeviceInfo::print(const char *tab) const
{
    ESP_LOGI(TAG, "%s======================================", tab);
    ESP_LOGI(TAG, "%sName: %.*s", tab, name.size(), name.c_str());
    ESP_LOGI(TAG, "%sType: %s", tab, isBle ? "BLE" : "Classic");
    ESP_LOGI(TAG, "%sAddr: %s", tab, bda2str(addr).c_str());
    ESP_LOGI(TAG, "%sRSSI: %d", tab, rssi);
    if (isBle) {
        printBle(tab);
    }
    else {
        printClassic(tab);
    }
}
void BluetoothStack::DeviceInfo::printClassic(const char* tab) const
{
    ESP_LOGI(TAG, "%sClass of Device: %s", tab, codInfo(bt.cod, bt.codRaw).c_str());
    ESP_LOGI(TAG, "%sUsage: %s", tab, esp_hid_usage_str(esp_hid_usage_from_cod(bt.codRaw)));
    const char* uuid = bt.uuid.len ? uuid2str(bt.uuid) : "?";
    ESP_LOGI(TAG, "%sUUID: %s", tab, uuid ? uuid : "null");
}
void BluetoothStack::DeviceInfo::printBle(const char *tab) const
{
  ESP_LOGI(TAG, "%sUUID: 0x%04x", tab, ble.uuid);
  ESP_LOGI(TAG, "%sAPPEARANCE: 0x%04x", tab, ble.appearance);
  ESP_LOGI(TAG, "%sUSAGE: %s", tab, magic_enum::enum_name(esp_hid_usage_from_appearance(ble.appearance)).data());
  ESP_LOGI(TAG, "%sADDR_TYPE: '%s'", tab, bleAddrTypeToStr(ble.addrType));
}
template<class T>
void BluetoothStack::Discovery::addDevice(T& info)
{
    if (this->devices.contains(info.bda)) {
        return;
    }
    auto& item = *this->devices.emplace(info).first;
    item.print();
    if (onNewDevice) {
        if (!onNewDevice(item)) {
            BtStack.stopDiscovery();
        }
    }
}
/*
void BluetoothStack::Discovery::addBleDevice(esp_ble_gap_cb_param_t::ble_scan_result_evt_param& info)
{
    // if (uuid == ESP_GATT_UUID_HID_SVC)
    // FIXME: We may have a classic BT and a BLE device with the same addr
    auto& item = mDevices.emplace(info.bda, info).first->second.print();
}
*/
BluetoothStack::DeviceInfo::DeviceInfo(struct esp_bt_gap_cb_param_t::disc_res_param& info)
:isBle(false)
{
    memcpy(addr, info.bda, sizeof(addr));
    for (int i = 0; i < info.num_prop; i++) {
        auto& p = info.prop[i];
        switch (p.type) {
            case ESP_BT_GAP_DEV_PROP_COD:
                bt.codRaw = *(uint32_t *)(p.val);
                continue;
            case ESP_BT_GAP_DEV_PROP_RSSI:
                rssi = *(int8_t *)(p.val);
                continue;
            case ESP_BT_GAP_DEV_PROP_BDNAME:
                if (name.empty()) {
                    name.assign((char*)p.val, p.len);
                }
                continue;
            case ESP_BT_GAP_DEV_PROP_EIR: {
                if (getUuidFromEir((uint8_t*)p.val, bt.uuid)) {
                    continue;
                }
                char buf[128];
                if (getNameFromEir((uint8_t*)p.val, buf, sizeof(buf))) {
                    ESP_LOGI(TAG, "\tDevice name from EIR: %s", buf);
                    if (name.empty()) {
                        name = buf;
                    }
                    continue;
                }
                ESP_LOGW(TAG, "\tCan't get UUID or name from EIR");
                continue;
            }
            default:
                ESP_LOGI(TAG, "Unknown GAP property of type %s(%d)", magic_enum::enum_name(p.type).data(), p.type);
                continue;
        }
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
        sprintf(str, "%08lx", uuid.uuid.uuid32);
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

#define EVTCASE(name) case ESP_BT_GAP_##name: return #name
const char* gapEventToStr(esp_bt_gap_cb_event_t event)
{
    switch(event) {
        EVTCASE(ACL_CONN_CMPL_STAT_EVT);
        EVTCASE(ACL_DISCONN_CMPL_STAT_EVT);
        default: return "(unknown)";
    }
}
#define ADDRT_CASE(name) case BLE_ADDR_TYPE_##name: return #name
const char* bleAddrTypeToStr(esp_ble_addr_type_t type)
{
    switch(type) {
        ADDRT_CASE(PUBLIC);
        ADDRT_CASE(RANDOM);
        ADDRT_CASE(RPA_PUBLIC);
        ADDRT_CASE(RPA_RANDOM);
        default: return "UNKNOWN";
    }
}
#if CONFIG_BT_BLE_ENABLED
#define KTCASE(name) case ESP_LE_KEY_##name: return #name
const char* bleKeyTypeToStr(esp_ble_key_type_t key_type)
{
    switch (key_type) {
        KTCASE(NONE);
        KTCASE(PENC);
        KTCASE(PID);
        KTCASE(PCSRK);
        KTCASE(PLK);
        KTCASE(LLK);
        KTCASE(LENC);
        KTCASE(LID);
        KTCASE(LCSRK);
        default: return "(invalid)";
    }
}
#endif /* CONFIG_BT_BLE_ENABLED */
