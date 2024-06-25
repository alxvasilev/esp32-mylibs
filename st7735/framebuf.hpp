#ifndef __FRAMEBUF_HPP
#define __FRAMEBUF_HPP
#include "esp_heap_caps.h"
#include <memory>
#define FRAMEBUF_DEBUG 1
#if FRAMEBUF_DEBUG
    #define fbassert(cond) if (!(cond)) { printf("Framebuf: assert(%s) failed\n", #cond); abort(); }
    #define FRAMEBUF_LOG(fmt,...) ESP_LOGI("FB", fmt, ##__VA_ARGS__)
#else
    #define fbassert(cond)
    #define FRAMEBUF_LOG(fmt,...)
#endif


template <class C>
class FrameBuffer
{
public:
    typedef C Color;
protected:
    typedef int16_t Coord;
    Coord mWidth;
    Coord mHeight;
    std::unique_ptr<Color> mBuf;
    Color* mBufEnd;
    Color* mWinEnd = nullptr;
    Color* mLineEnd = nullptr;
    Color* mNextPixel = nullptr;
    Coord mWinWidth = 0;
    Coord mLineSkip = 0;
public:
    FrameBuffer(Coord width, Coord height, int allocFlags)
        : mWidth(width), mHeight(height),
          mBuf((Color*)heap_caps_malloc(width * height * sizeof(Color), allocFlags)),
          mBufEnd(mBuf.get() + width * height) {}
    FrameBuffer(): mWidth(0), mHeight(0), mBufEnd(nullptr) {}
    // interface used by Gfx
    Coord width() const { return mWidth; }
    Coord height() const { return mHeight; }
    Color* data() const { return mBuf.get(); }
    int byteSize() const { return mWidth * mHeight * sizeof(Color); }
    Color* nextPixel() const { return mNextPixel; }
    void reset(Coord width, Coord height, Color* buf) {
        mWidth = width;
        mHeight = height;
        mBufEnd = buf ? (buf + width * height) : nullptr;
        mBuf.reset(buf);
        mNextPixel = nullptr;
    }
    void prepareSendPixels() {}
    void sendNextPixel(Color pixel)
    {
        *(mNextPixel++) = pixel;
        if (mNextPixel >= mLineEnd) {
            mNextPixel += mLineSkip;
            if (mNextPixel >= mWinEnd) {
                mNextPixel = nullptr;
                mLineEnd = nullptr;
            }
            else {
                mLineEnd = mNextPixel + mWinWidth;
            }
        }
    }
    void setWriteWindow(Coord x, Coord y, Coord w, Coord h)
    {
        FRAMEBUF_LOG("setWriteWindow: x=%d, y=%d, w=%d, h=%d, mWidth=%d, mHeight=%d", x, y, w, h, mWidth, mHeight);
        fbassert(x >= 0 && x + w <= mWidth);
        fbassert(y >= 0 && y + h <= mHeight);
        mWinWidth = w;
        mNextPixel = mBuf.get() + y * mWidth + x;
        mLineEnd = mNextPixel + w;
        mLineSkip = mWidth - w;
        mWinEnd = mNextPixel + (h - 1) * mWidth + w;
        fbassert(mWinEnd <= mBufEnd);
    }
    void setPixel(Coord x, Coord y, Color color)
    {
        fbassert(x >= 0 && x <= mWidth);
        fbassert(y >= 0 && y <= mHeight);
        auto pixPtr = mBuf.get() + y * mWidth + x;
        fbassert(pixPtr >= mBufEnd);
        *pixPtr = color;
    }
    void fillRect(Coord x, Coord y, Coord w, Coord h, Color color)
    {
        FRAMEBUF_LOG("fillRect: x=%d, y=%d, w=%d, h=%d, mWidth=%d, mHeight=%d", x, y, w, h, mWidth, mHeight);
        fbassert(x >= 0 && x <= mWidth);
        fbassert(y >= 0 && y <= mHeight);
        fbassert(x + w <= mWidth);
        fbassert(y + h <= mHeight);
        int lineSkip = mWidth - w;
        Color* pixPtr = mBuf.get() + y * mWidth + x;
        for (int line = 0; line < h; line++) {
            auto lineEnd = pixPtr + w;
            while(pixPtr < lineEnd) {
                *(pixPtr++) = color;
            }
            pixPtr += lineSkip;
        }
    }
};

#endif
