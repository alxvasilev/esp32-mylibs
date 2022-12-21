#ifndef WATCHDOG_HPP
#define WATCHDOG_HPP

#include <esp_timer.h>
#include "mutex.hpp"

class PingWatchdog {
protected:
    typedef PingWatchdog Self;
    esp_timer_handle_t mPingTimer;
    esp_timer_handle_t mPongTimer;
    typedef void(*Callback)(void*);
    Callback mPingCb;
    Callback mToutCb;
    void* mCbArg;
    uint32_t mPingIntervalUs = 0;
    uint32_t mTimeoutUs = 0;
    Mutex mMutex;
    enum: uint8_t {
        kPingTimer = 1,
        kPongTimer = 2
    };
    uint8_t mState = 0;
    static constexpr const char* kTAG = "PING_WDT";
public:
    bool isRunning() const { return mState & (kPingTimer | kPongTimer); }
    PingWatchdog(Callback pingCb, Callback toutCb, void* arg)
    : mPingCb(pingCb), mToutCb(toutCb), mCbArg(arg) {}
    void init()
    {
        esp_timer_create_args_t args = {
            .callback = &onPingTimer,
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "PingWdt_ping",
            .skip_unhandled_events = true
        };
        ESP_ERROR_CHECK(esp_timer_create(&args, &mPingTimer));
        args.callback = &onPongTimer;
        args.name = "PingWdt_tout";
        ESP_ERROR_CHECK(esp_timer_create(&args, &mPongTimer));
    }
    void start(uint32_t intervalMs, uint32_t timeoutMs)
    {
        MutexLocker locker(mMutex);
        if (this->isRunning()) {
            stop();
        }
        mPingIntervalUs = intervalMs * 1000;
        mTimeoutUs = timeoutMs * 1000;
        mState = kPingTimer;
        esp_timer_start_once(mPingTimer, mPingIntervalUs);
    }
    void stop()
    {
        MutexLocker locker(mMutex);
        if (mState == kPingTimer) {
            esp_timer_stop(mPingTimer);
        } else if (mState == kPongTimer) {
            esp_timer_stop(mPongTimer);
        }
        mState = 0;
    }
    void pong()
    {
        MutexLocker locker(mMutex);
        if (mState != kPongTimer) {
            return;
        }
        esp_timer_stop(mPongTimer);
        esp_timer_start_once(mPingTimer, mPingIntervalUs);
        mState = kPingTimer;
    }
    ~PingWatchdog()
    {
        stop();
        esp_timer_delete(mPongTimer);
        esp_timer_delete(mPingTimer);
    }
protected:
    static void onPingTimer(void* arg)
    {
        auto& self = *static_cast<Self*>(arg);
        MutexLocker locker(self.mMutex);
        if (self.mState != kPingTimer) {
            return;
        }
        self.mPingCb(self.mCbArg);
        esp_timer_start_once(self.mPongTimer, self.mTimeoutUs);
        self.mState = kPongTimer;
    }
    static void onPongTimer(void* arg)
    {
        auto& self = *static_cast<Self*>(arg);
        MutexLocker locker(self.mMutex);
        if (self.mState != kPongTimer) {
            return;
        }
        self.mToutCb(self.mCbArg);
        esp_timer_start_once(self.mPingTimer, self.mPingIntervalUs);
        self.mState = kPingTimer;
    }
};

#endif // WATCHDOG_HPP
