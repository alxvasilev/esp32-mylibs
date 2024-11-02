#ifndef BLUETOOTH_STACK_HPP
#define BLUETOOTH_STACK_HPP

#include <esp_bt.h>
#include <esp_gap_bt_api.h>
#include <esp_avrc_api.h>
#include <esp_hid_common.h>
#include <string>
#include <map>
#include <array>
#include <memory>

class BluetoothStack;
extern BluetoothStack BtStack;

class BluetoothStack
{
public:
    struct DeviceInfo {
        union {
            struct{
                union {
                    esp_bt_cod_t cod;
                    uint32_t codRaw;
                };
                esp_bt_uuid_t uuid;
            } bt;
            struct {
              esp_ble_addr_type_t addr_type;
              uint16_t appearance;
            } ble;
        };
        std::string name;
        int rssi;
        bool isBle = false;
        DeviceInfo() { bt.uuid.len = 0; }
        bool fillFromGapProp(esp_bt_gap_dev_prop_t& p);
        bool isPeripheral(uint8_t type) const;
        bool isKeyboard() const { return isPeripheral(ESP_HID_COD_MIN_KEYBOARD); }
        bool isMouse() const {return isPeripheral(ESP_HID_COD_MIN_MOUSE); }
        void print(const char* tab="\t");
        void printUuid(const char* tab="\t");
    };
    struct Addr: public std::array<uint8_t, ESP_BD_ADDR_LEN> {
        std::string toString() const { return bda2str(data()); }
    };
    typedef std::map<Addr, DeviceInfo> DeviceList;
    struct Discovery {
        typedef void(*DiscoCompleteCb)(DeviceList&);
        DeviceList mDevices;
        DiscoCompleteCb onComplete;
        void addDevice(esp_bt_gap_cb_param_t *param);
        static void defltCompleteCb(DeviceList&) {}
        Discovery(DiscoCompleteCb cb = defltCompleteCb): onComplete(cb) {}
    };
    typedef void(*ShowPinFunc)(uint32_t code);
    typedef int8_t(*EnterPinFunc)(uint8_t* pinbuf, uint8_t bufSize);
protected:
    static constexpr const char* TAG = "btstack";
    std::unique_ptr<Discovery> mDiscovery;
#if (CONFIG_BT_SSP_ENABLED == true)
    ShowPinFunc mShowPinCb = nullptr;
#ifdef MYBT_PIN_INPUT
    EnterPinFunc mEnterPinCb = nullptr;
#endif
#endif
    esp_bt_mode_t mMode = ESP_BT_MODE_IDLE;
    static void gapCallback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);
    static void avrcCallback(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param);
public:
    static constexpr BluetoothStack& self() { return BtStack; }
    esp_bt_mode_t mode() const { return mMode; }
    bool started() const { return mMode != ESP_BT_MODE_IDLE; }
#if (CONFIG_BT_SSP_ENABLED == true)
#ifdef MYBT_PIN_INPUT
    void setupPinAuth(ShowPinFunc showPin, EnterPinFunc enterPin);
#else
    void setupPinAuth(ShowPinFunc showPin);
#endif
    void setPin(uint8_t* pin, int pinLen);
#endif
    bool start(esp_bt_mode_t mode, const char* discoName);
    void shutdown();
    void disable(esp_bt_mode_t mode); // releases BT stack memory pools
    void disableCompletely() { disable(ESP_BT_MODE_BTDM); }
    void disableBLE() { disable(ESP_BT_MODE_BLE); }
    void disableClassic() { disable(ESP_BT_MODE_CLASSIC_BT); }
    esp_err_t becomeDiscoverableAndConnectable();
    template <class Disco=Discovery, typename... Args>
    void discoverDevices(float secs, Args... args) {
        if (mDiscovery) {
            ESP_LOGE("btstack", "Discovery already in progress");
            return;
        }
        mDiscovery.reset(new Disco(std::forward<Args>(args)...));
        esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, int(secs / 1.28 + .5), 0);
    }
    static const char* bda2str(const uint8_t* bda, char* buf, int bufLen);
    static std::string bda2str(const uint8_t* bda);
    static const char* uuid2str(const esp_bt_uuid_t& uuid);
};
#endif
