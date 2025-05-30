#ifndef RINGBUF_HPP
#define RINGBUF_HPP

#include <sys/unistd.h>
#include <sys/stat.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "utils.hpp"
#include "waitable.hpp"

#define rbassert myassert

class RingBuf;
struct ReadBuf
{
    RingBuf* ringBuf;
    char* buf = nullptr;
    int size;
    inline ~ReadBuf();
};

class RingBuf: public Waitable
{
protected:
    char* mBuf;
    char* mBufEnd;
    char* mWritePtr;
    char* mReadPtr;
    Mutex mMutex;
    // Prevents clearing the ringbuffer while someone is using the
    // buffer returned by contigRead()
    int mDataSize;
    int bufSize() const { return mBufEnd - mBuf; }
    int availableForContigRead()
    {
        if (mWritePtr > mReadPtr) { // none wrapped
            return mWritePtr - mReadPtr;
        } else if (mReadPtr > mWritePtr) { // write wrapped, read didn't
            return mBufEnd - mReadPtr;
        } else if (mEvents.get() & kFlagHasSpace) { // empty
            return 0;
        } else { // full
            myassert(mEvents.get() & kFlagHasItems);
            return mBufEnd - mReadPtr;
        }
    }
    int maxPossibleContigReadSize() { return mBufEnd - mReadPtr; }
    int availableForContigWrite()
    {
        if (mWritePtr > mReadPtr) {
            return mBufEnd - mWritePtr;
        } else if (mWritePtr < mReadPtr){
            return mReadPtr - mWritePtr; // Removed: artificially leave 1 byte to distinguish between empty and full
        } else { // empty or full
            return (mDataSize == 0) ? (mBufEnd - mWritePtr) : 0;
        }
    }
    void doCommitContigRead(int size)
    {
        rbassert(size <= availableForContigRead());
        mReadPtr += size;
        if (mReadPtr == mBufEnd) {
            mReadPtr = mBuf;
        } else {
            assert(mReadPtr < mBufEnd);
        }
        mDataSize -= size;
        if (mWritePtr == mReadPtr) {
            mEvents.clearBits(kFlagHasItems);
            mEvents.setBits(kFlagIsEmpty);
        }
        mEvents.setBits(kFlagReadOp|kFlagHasSpace);
    }
    void commitContigWrite(int size)
    {
        rbassert(size <= availableForContigWrite());
        mWritePtr += size;
        if (mWritePtr >= mBufEnd) {
            myassert(mWritePtr == mBufEnd);
            mWritePtr = mBuf;
        } else {
            rbassert(mWritePtr < mBufEnd);
        }
        mDataSize += size;
        EventBits_t bitsToClear = kFlagIsEmpty;
        if (mWritePtr == mReadPtr) {
            bitsToClear |= kFlagHasSpace;
        }
        mEvents.clearBits(bitsToClear);
        mEvents.setBits(kFlagWriteOp | kFlagHasItems);
    }
    int contigWrite(char* buf, int size)
    {
        int wlen = std::min(size, availableForContigWrite());
        myassert(mWritePtr+wlen <= mBufEnd);
        memcpy(mWritePtr, buf, wlen);
        commitContigWrite(wlen);
        return wlen;
    }
    int totalEmptySpace_nolock()
    {
        return size() - mDataSize;
    }
    void doClear()
    {
        mDataSize = 0;
        mWritePtr = mReadPtr = mBuf;
        mEvents.clearBits(0xff);
        mEvents.setBits(kFlagHasSpace|kFlagIsEmpty|kFlagReadOp);
        assert(mEvents.get() == (kFlagHasSpace|kFlagIsEmpty|kFlagReadOp));
    }
public:
    // If user wants to keep some external state in sync with the ringbuffer,
    // they can use the ringbuf's mutex to protect that state
    Mutex& mutex() { return mMutex; }
    RingBuf(size_t bufSize, bool useSpiRam=false)
        : mBuf((char*)heap_caps_malloc(bufSize, useSpiRam ? MALLOC_CAP_8BIT|MALLOC_CAP_SPIRAM : MALLOC_CAP_8BIT))
    {
        if (!mBuf) {
            ESP_LOGE("RINGBUF", "Out of memory allocation %zu bytes", bufSize);
            return;
        }
        mBufEnd = mBuf + bufSize;
        doClear();
    }
    ~RingBuf()
    {
        if (mBuf) {
            free(mBuf);
        }
    }
    int size() const { return mBufEnd - mBuf; }
    void clear()
    {
        MutexLocker locker(mMutex);
        for (;;) {
            if (!mOpInProgress) {
                doClear();
                return;
            }
            {
                // an operation is in progress, wait for its completion
                MutexUnlocker unlocker(mMutex);
                waitFor(kFlagReadOp | kFlagWriteOp, -1);
            }
        }
    }
    int dataSize() const
    {
        return mDataSize;
    }
    int totalEmptySpace()
    {
        MutexLocker locker(mMutex);
        return totalEmptySpace_nolock();
    }
    /* Read requested amount and block if needed.
     * @returns 1 upon success, 0 upon timeout, -1 if stop was signalled
     */
    int8_t read(char* buf, int size, int msTimeout)
    {
        for (;;) {
            int64_t tsStart = esp_timer_get_time();
            mMutex.lock();
            if (dataSize() >= size) {
                break;
            }
            mMutex.unlock();
            auto ret = waitAndReset(kFlagWriteOp, msTimeout);
            if (ret <= 0) {
                return ret;
            }
            if (msTimeout > 0) {
                msTimeout -= (esp_timer_get_time() - tsStart) / 1000;
                if (msTimeout < 0) {
                    return 0;
                }
            }
        }
        // mutex is locked here
        auto rlen = std::min(size, availableForContigRead());
        memcpy(buf, mReadPtr, rlen);
        doCommitContigRead(rlen);
        if (rlen >= size) {
            rbassert(rlen == size);
            mMutex.unlock();
            return 1;
        }
        buf += rlen;
        size -= rlen;
        memcpy(buf, mReadPtr, size);
        doCommitContigRead(size);
        mMutex.unlock();
        return 1;
    }
    /* Returns a contiguous buffer with data for reading, which may be shorter
     * than sizeWanted. If no data is available for reading, blocks until data becomes
     * available or timeout elapses
     * @returns the amount of data in the returned buffer, 0 for timeout or -1 if stop
     * was signalled
     */
    int contigRead(char*& buf, int maxSize, int msTimeout)
    {
        MutexLocker locker(mMutex);
        int avail;
        if (msTimeout < 0) {
            while ((avail = availableForContigRead()) < 1) {
                MutexUnlocker unlocker(mMutex);
                //ESP_LOGI("RB", "underflow");
                if (waitForWriteOp(-1) <= 0) {
                    return -1;
                }
            }
        } else {
            while ((avail = availableForContigRead()) < 1) {
                int64_t tsStart = esp_timer_get_time();
                MutexUnlocker unlocker(mMutex);
                //ESP_LOGI("RB", "underflow");
                int ret = waitForWriteOp(msTimeout);
                msTimeout -= (esp_timer_get_time() - tsStart) / 1000;
                if (ret < 0) {
                    return ret;
                }
                if (msTimeout < 0) {
                    return 0;
                }
            }
        }
        mOpInProgress |= kReadInProgress;
        buf = mReadPtr;
        return avail > maxSize ? maxSize : avail;
    }
    void contigRead(ReadBuf& readBuf, int msTimeout)
    {
        readBuf.ringBuf = this;
        int ret = contigRead(readBuf.buf, readBuf.size, msTimeout);
        readBuf.size = ret;
        if (ret <= 0) {
            readBuf.buf = nullptr;
        }
    }
    const char* peek(size_t len, char* buf, int msTimeout=-1)
    {
        while (mDataSize < len) {
            if (!msTimeout) {
                return nullptr;
            }
            if (waitForWriteOp(msTimeout) <= 0) {
                return nullptr;
            }
        }
        MutexLocker locker(mMutex);
        auto contig = availableForContigRead();
        if (contig >= len) {
            return mReadPtr;
        }
        if (contig) {
            memcpy(buf, mReadPtr, contig);
        }
        len -= contig;
        memcpy(buf + contig, mBuf, len);
        return buf;
    }
    bool write(char* buf, int size)
    {
        for (;;) {
            mMutex.lock();
            if (totalEmptySpace_nolock() >= size) {
                break;
            }

            mMutex.unlock();
            if (waitAndReset(kFlagReadOp, -1) < 0) {
                return false;
            }
        }
        // mutex is locked here
        auto written = contigWrite(buf, size);

        if (written < size) {
            buf += written;
            size -= written;
            written = contigWrite(buf, size);

            rbassert(written == size);
        }
        mMutex.unlock();
        return true;
    }
    int getWriteBuf(char*& buf, int reqSize, int timeoutMs)
    {
        MutexLocker locker(mMutex);
        int maxPossible = mBufEnd - mWritePtr;
        if (reqSize > maxPossible) {
            reqSize = maxPossible;
        }
        for (;;) {
            auto contig = availableForContigWrite();
            if (contig >= reqSize) {
                buf = mWritePtr;
                mOpInProgress |= kWriteInProgress;
                return contig;
            }
            {
                MutexUnlocker unlocker(mMutex);
                auto ret = waitForReadOp(timeoutMs);
                if (ret <= 0) {
                    buf = nullptr;
                    return ret;
                }
            }
        }
    }
    void commitWrite(int size) {
        MutexLocker locker(mMutex);
        commitContigWrite(size);
        mOpInProgress &= ~kWriteInProgress;
    }
    void abortWrite() {
        MutexLocker locker(mMutex);
        mOpInProgress &= ~kWriteInProgress;
    }
    void commitContigRead(int size)
    {
        {
            MutexLocker locker(mMutex);
            doCommitContigRead(size);
            mOpInProgress &= ~kReadInProgress;
        }
    }
    bool hasData() const
    {
        return ((mEvents.get() & kFlagHasItems) != 0);
    }
};

inline ReadBuf::~ReadBuf()
{
    if (buf) {
        ringBuf->commitContigRead(size);
    }
}

#endif
