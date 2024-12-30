#ifndef WAITABLE_HPP
#define WAITABLE_HPP

#include <sys/unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "eventGroup.hpp"

class Waitable
{
protected:
    enum: uint8_t { kFlagHasItems = 1, kFlagIsEmpty = 2, kFlagHasSpace = 4,
                    kFlagWriteOp = 8, kFlagReadOp = 16, kFlagStop = 32, kFlagLast = kFlagStop };
    enum: uint8_t { kReadInProgress = 1, kWriteInProgress = 2 };
    EventGroup mEvents;
    uint8_t mOpInProgress = 0;
    // -1: stopped, 0: timeout, 1: event occurred
    int8_t waitFor(uint32_t flags, int msTimeout)
    {
        auto bits = mEvents.waitForOneNoReset(flags | kFlagStop, msTimeout);
        if (bits & kFlagStop) {
            return -1;
        } else if (!bits) { //timeout
            return 0;
        }
        assert(bits & flags);
        return 1;
    }
    int8_t waitAndReset(EventBits_t flag, int msTimeout)
    {
        auto bits = mEvents.waitForOneAndReset(flag | kFlagStop, msTimeout);
        if (bits & kFlagStop) {
            return -1;
        } else if (bits == 0) {
            return 0;
        } else {
            assert(bits == flag);
            return 1;
        }
    }
public:
    Waitable(): mEvents(kFlagStop) {}
    void setStopSignal()
    {
        mEvents.setBits(kFlagStop);
    }
    void clearStopSignal()
    {
        mEvents.clearBits(kFlagStop);
    }
    int8_t waitForItems(int msTimeout)
    {
        return waitFor(kFlagHasItems, msTimeout);
    }
    bool waitForEmpty()
    {
        return waitFor(kFlagIsEmpty, -1) >= 0;
    }
    int8_t waitForWriteOp(int msTimeout) { return waitAndReset(kFlagWriteOp, msTimeout); }
    int8_t waitForReadOp(int msTimeout) { return waitAndReset(kFlagReadOp, msTimeout); }
};

#endif
