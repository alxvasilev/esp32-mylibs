#ifndef BLUETOOTH_STACK_HPP
#define BLUETOOTH_STACK_HPP

#include <esp_bt.h>
#include <esp_gap_bt_api.h>
#include <esp_hid_common.h>
#if CONFIG_BT_BLE_ENABLED
    #include <esp_gap_ble_api.h>
#endif
#include <string>
#include <string.h> // for memcpy
#include <set>
#include <array>
#include <memory>

class BluetoothStack;
extern BluetoothStack BtStack;

class BluetoothStack
{
public:
    struct DeviceInfo {
        esp_bd_addr_t addr;
        bool isBle = false;
        std::string name;
        int rssi = 0;
        union {
            struct{
                union {
                    esp_bt_cod_t cod;
                    uint32_t codRaw = 0;
                };
                esp_bt_uuid_t uuid = {.len=0, .uuid=0};
            } bt;
            struct {
              esp_ble_addr_type_t addrType;
              uint16_t appearance = 0;
              uint16_t uuid = 0;
            } ble;
        };
#ifdef CONFIG_BT_BLE_ENABLED
        DeviceInfo(esp_ble_gap_cb_param_t::ble_scan_result_evt_param& info);
#endif
        DeviceInfo(esp_bt_gap_cb_param_t::disc_res_param& info);
        bool operator<(const DeviceInfo& other) const {
            return memcmp(addr, other.addr, sizeof(addr)) < 0;
        }
        bool operator<(const esp_bd_addr_t& other) const {
            static_assert(sizeof(addr) == sizeof(other), "");
            return memcmp(addr, other, sizeof(addr)) < 0;
        }
        bool isPeripheral(uint8_t type) const;
        bool isKeyboard() const { return isPeripheral(ESP_HID_COD_MIN_KEYBOARD); }
        bool isMouse() const {return isPeripheral(ESP_HID_COD_MIN_MOUSE); }
        void print(const char* tab="\t") const;
        std::string addrString() const { return bda2str(addr); }
    protected:
        void printClassic(const char* tab) const;
#ifdef CONFIG_BT_BLE_ENABLED
        void printBle(const char* tab) const;
#endif
    };
    typedef std::set<DeviceInfo, std::less<>> DeviceList;
    struct Discovery {
        typedef void(*DiscoCompleteCb)(const DeviceList&);
        typedef bool(*NewDeviceCb)(const DeviceInfo&);
        esp_bt_mode_t mode;
        DeviceList devices;
        NewDeviceCb onNewDevice;
        DiscoCompleteCb onComplete;
        template<class T>
        void addDevice(T& info);
        static void defltCompleteCb(const DeviceList&) {}
        Discovery(esp_bt_mode_t aMode, NewDeviceCb deviceCb, DiscoCompleteCb complCb = defltCompleteCb)
        : mode(aMode), onNewDevice(deviceCb), onComplete(complCb) {}
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
#if CONFIG_BT_BLE_ENABLED
    static void bleGapCallback(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t * param);
#endif
    bool doDiscoverDevices(float secs, Discovery* disco);
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
    esp_err_t becomeDiscoverableAndConnectable(bool enable=true);
    template <class T=Discovery, typename... Args>
    bool discoverDevices(float secs, esp_bt_mode_t scanMode, const Args&... args) {
        if (mDiscovery) {
            ESP_LOGE("btstack", "Discovery already in progress");
            return true;
        }
        scanMode = (esp_bt_mode_t)(scanMode & mMode);
        return doDiscoverDevices(secs, new T(scanMode, args...));
    }
    void stopDiscovery();
    static const char* bda2str(const uint8_t* bda, char* buf, int bufLen);
    static std::string bda2str(const uint8_t* bda);
    static const char* uuid2str(const esp_bt_uuid_t& uuid);
};
static inline bool operator<(const esp_bd_addr_t& first, const BluetoothStack::DeviceInfo& second) {
    return memcmp(first, second.addr, sizeof(second.addr)) < 0;
}

#endif
