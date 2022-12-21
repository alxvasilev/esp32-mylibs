#ifndef _WIFI_HPP
#define _WIFI_HPP
#include <esp_wifi.h>
#include "eventGroup.hpp"

class WifiBase
{
protected:
    EventGroup mEvents;
    const bool mIsAp;
    esp_ip4_addr mLocalIp;
    void eventLoopCreateAndRegisterHandler(esp_event_handler_t handler);
public:
    enum { kBitConnected = 1, kBitAborted = 2 };
    WifiBase(bool isAp): mEvents(kBitAborted), mIsAp(isAp){}
    virtual bool start(const char* ssid, const char* key) = 0;
    virtual void stop() = 0;
    virtual ~WifiBase() {}
    bool isAp() const { return mIsAp; }
    const esp_ip4_addr& localIp() const { return mLocalIp; }
    virtual bool waitForConnect(int msTimeout) = 0;
};

class WifiClient: public WifiBase
{
public:
    typedef void(*ConnGaveUpHandler)();
protected:
    enum { kMaxConnectRetries = 10 };
    int mRetryNum = 0;
    ConnGaveUpHandler mConnRetryGaveUpHandler;
    static void wifiEventHandler(void* userp, esp_event_base_t event_base,
                                  int32_t event_id, void* event_data);
    static void gotIpEventHandler(void* userp, esp_event_base_t event_base,
                                  int32_t event_id, void* event_data);
public:
    WifiClient(): WifiBase(false) {}
    ~WifiClient() override;
    bool start(const char* ssid, const char* key, ConnGaveUpHandler onGaveUp);
    virtual bool start(const char *ssid, const char *key) {
        return start(ssid, key, nullptr);
    }
    virtual void stop() override;
    virtual bool waitForConnect(int msTimeout) override;
};

class WifiAp: public WifiBase
{
protected:
    static void apEventHandler(void* userp, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data);
    void reconfigDhcpServer();
public:
    WifiAp(): WifiBase(true) {}
    ~WifiAp() override;
    void start(const char* ssid, const char* key, uint8_t chan);
    virtual bool start(const char *ssid, const char *key) {
        start(ssid, key, 0);
        return true;
    }
    virtual void stop() override;
    virtual bool waitForConnect(int msTimeout) override {return false;} //TODO: Implement waiting a wifi client to connect
};

#endif
