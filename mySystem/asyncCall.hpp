#include "eventGroup.hpp"
#include "mutex.hpp"
#include "funcTraits.hpp"
#include <type_traits>
#include <utility>
#include <exception>
#include <memory>
#include <type_traits>

template <class Cb>
static void asyncCall(Cb&& func, uint32_t delayTicks = 1)
{
    struct AsyncMsg {
        TimerHandle_t mTimer;
        Cb mCallback;
        AsyncMsg(Cb&& cb, uint32_t delay): mCallback(std::forward<Cb>(cb))
        {
            mTimer = xTimerCreate("asynCall", delay, pdFALSE, this, &onTimer);
            xTimerStart(mTimer, portMAX_DELAY);
        }
        ~AsyncMsg()
        {
            xTimerDelete(mTimer, portMAX_DELAY);
        }
        static void onTimer(TimerHandle_t xTimer)
        {
            auto self = static_cast<AsyncMsg*>(pvTimerGetTimerID(xTimer));
#ifdef __EXCEPTIONS
            try {
#endif
                self->mCallback();
#ifdef __EXCEPTIONS
            } catch(std::exception& e) {}
#endif
            delete self;
        }
    };
    new AsyncMsg(std::forward<Cb>(func), delayTicks);
}

struct BlockingAsyncCtx {
#if configUSE_16_BIT_TICKS
    enum { kNumEventBits = 8 };
    typedef uint8_t SlotBits;
#else
    enum { kNumEventBits = 24 };
    typedef uint32_t SlotBits;
#endif
    enum { kNumSlots = kNumEventBits - 1,
           kMaxSlot = 1 << (kNumSlots - 1),
           kEvtCallSlotFreed = 1 << (kNumSlots - 1)
    };
    EventGroup mEvents;
    Mutex mMutex;
    SlotBits mSlots;
    SlotBits mLastSlot = 1;
    SlotBits allocSlot() // finds an empty slot in the event group
    {
        MutexLocker locker(mMutex);
        for (;;) {
            SlotBits startSlot = mLastSlot;
            SlotBits slot = startSlot;
            do {
                if ((mSlots & slot) == 0) {
                    mSlots |= slot;
                    mLastSlot = slot;
                    mEvents.clearBits(slot);
                    return slot;
                }
                slot <<= 1;
                if (slot > kMaxSlot) {
                    slot = 1;
                }
            } while (slot != startSlot);
            {
                MutexUnlocker unlock(mMutex);
                mEvents.waitForOneAndReset(kEvtCallSlotFreed, -1);
            }
        }
    }
    void signalSlot(SlotBits slot) {
        mEvents.setBits(slot);
    }
    void releaseSlot(SlotBits slot) {
        MutexLocker locker(mMutex);
        mSlots &= ~slot;
    }
};

extern BlockingAsyncCtx gBlockingAsyncCtx;

template <class Cb>
struct BlockingCall {
    typedef FuncRet_t<Cb> RetType;
    enum { kIsVoidRet = std::is_same_v<RetType, void> };
    Cb mCallback;
    TimerHandle_t mTimer;
    BlockingAsyncCtx::SlotBits mSlot;
    std::conditional_t<kIsVoidRet, char[], RetType> mRetVal;
    BlockingCall(Cb&& cb, uint32_t delay)
    : mCallback(std::forward<Cb>(cb)), mSlot(gBlockingAsyncCtx.allocSlot())
    {
        mTimer = xTimerCreate("blockgCall", delay, pdFALSE, this, &onTimer);
        xTimerStart(mTimer, portMAX_DELAY);
    }
    ~BlockingCall()
    {
        xTimerDelete(mTimer, portMAX_DELAY);
        gBlockingAsyncCtx.releaseSlot(mSlot);
    }
    static void onTimer(TimerHandle_t xTimer)
    {
        auto self = static_cast<BlockingCall*>(pvTimerGetTimerID(xTimer));
#ifdef __EXCEPTIONS
        try {
#endif
            if (kIsVoidRet) {
                self->mCallback();
            }
            else {
                self->mRetVal = self->mCallback();
            }
#ifdef __EXCEPTIONS
        } catch(std::exception& e) {}
#endif
        gBlockingAsyncCtx.signalSlot(self->mSlot);
    }
    void wait() {
        gBlockingAsyncCtx.mEvents.waitForOneNoReset(mSlot, -1);
    }
};
template <class Cb>
static std::enable_if_t<!std::is_same_v<FuncRet_t<Cb>, void>, FuncRet_t<Cb>>
asyncCallWait(Cb&& func, uint32_t delayTicks = 1)
{
    std::unique_ptr<BlockingCall<Cb>> call(new BlockingCall(std::forward<Cb>(func), delayTicks));
    call->wait();
    return std::move(call->mRetVal);
}
template <class Cb>
static std::enable_if_t<std::is_same_v<FuncRet_t<Cb>, void>, void>
asyncCallWait(Cb&& func, uint32_t delayTicks = 1)
{
    std::unique_ptr<BlockingCall<Cb>> call(new BlockingCall(std::forward<Cb>(func), delayTicks));
    call->wait();
}
