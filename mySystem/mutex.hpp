#ifndef MUTEX_HPP_INCLUDED
#define MUTEX_HPP_INCLUDED

#ifndef AV_MUTEX_USE_STD
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class Mutex
{
    SemaphoreHandle_t mMutex;
    StaticSemaphore_t mMutexMem;
public:
    Mutex() {
        mMutex = xSemaphoreCreateRecursiveMutexStatic(&mMutexMem);
    }
    void lock() { xSemaphoreTakeRecursive(mMutex, portMAX_DELAY); }
    void unlock() { xSemaphoreGiveRecursive(mMutex); }
};

class MutexLocker
{
    Mutex& mMutex;
public:
    MutexLocker(Mutex& aMutex): mMutex(aMutex) { mMutex.lock(); }
    ~MutexLocker() { mMutex.unlock(); }
};

class MutexUnlocker
{
    Mutex& mMutex;
public:
    MutexUnlocker(Mutex& aMutex): mMutex(aMutex) { mMutex.unlock(); }
    ~MutexUnlocker() { mMutex.lock(); }
};
#else
#include <mutex>
typedef std::recursive_mutex Mutex;
struct MutexLocker: public std::unique_lock<Mutex> {
    using std::unique_lock<Mutex>::unique_lock;
};
struct MutexUnlocker {
    explicit MutexUnlocker(Mutex& aMutex): mMutex(aMutex) { aMutex.unlock(); }
    ~MutexUnlocker() { mMutex.lock(); }
    MutexUnlocker(const MutexUnlocker&) = delete;
    MutexUnlocker& operator=(const MutexUnlocker&) = delete;
protected:
    Mutex& mMutex;
};
#endif
#endif
