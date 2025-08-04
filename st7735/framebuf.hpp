#ifndef __FRAMEBUF_COLOR_HPP
#define __FRAMEBUF_COLOR_HPP
#include <memory>
//#define FRAMEBUF_DEBUG 1
#if FRAMEBUF_DEBUG
    #define fbassert(cond) if (!(cond)) { printf("Framebuf: assert(%s) failed\n", #cond); abort(); }
    #define FRAMEBUF_LOG(fmt,...) ESP_LOGI("FB", fmt, ##__VA_ARGS__)
#else
    #define fbassert(cond)
    #define FRAMEBUF_LOG(fmt,...)
#endif
// We need to pass an allocator to the ctor, but if construction is done in an initializer list,
// we can't pass a template param there, so we need to pass an instance of the allocator
// not just the type as a template param, so the allocator type can be deduced).
// Hence, the malloc function is an instance method and the malloc params are instance members
struct FrameBufColorDefaultAlloc {
    void* malloc(size_t size) const { return ::malloc(size); }
    static void free(void* ptr) { ::free(ptr); }
};
template <class C>
class FrameBufferColor
{
public:
    static constexpr const bool isMono = false;
    typedef C Color;
protected:
    typedef int16_t Coord;
    Coord mWidth;
    Coord mHeight;
    Color* mBuf = nullptr;
    void(*mFreeFunc)(void*);
    Color* mBufEnd;
    Color* mWinEnd;
    Color* mLineEnd;
    Color* mNextPixel;
    Coord mWinWidth;
    Coord mLineSkip;
public:
    template<class Alloc = FrameBufColorDefaultAlloc>
    FrameBufferColor(Coord width, Coord height, const Alloc& alloc = Alloc())
    {
        mFreeFunc = &Alloc::free;
        reset(width, height, (Color*)alloc.malloc(width * height * sizeof(Color)));
    }
    template<class Alloc = FrameBufColorDefaultAlloc>
    FrameBufferColor(const Alloc& alloc = Alloc())
    {
        mFreeFunc = &Alloc::free;
        reset(0, 0, nullptr);
    }
    ~FrameBufferColor() { mFreeFunc((void*)mBuf); }
    void init() { memset(mBuf, 0, (mBufEnd - mBuf) * sizeof(Color)); }
    // interface used by Gfx
    Coord width() const { return mWidth; }
    Coord height() const { return mHeight; }
    Color* frameBuf() { return mBuf; }
    const Color* frameBuf() const { return mBuf; }
    Color* frameBufEnd() const { return mBufEnd; }
    int frameBufSize() const { return mWidth * mHeight * sizeof(Color); }
    Color* nextPixel() { return mNextPixel; }
    const Color* nextPixel() const { return mNextPixel; }
    void reset(Coord width, Coord height, Color* buf) {
        mWidth = width;
        mHeight = height;
        mNextPixel = mWinEnd = mLineEnd = nullptr;
        mWinWidth = mLineSkip = 0;
        mFreeFunc(mBuf);
        mBufEnd = buf ? (buf + width * height) : nullptr;
        mBuf = buf;
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
        mNextPixel = mBuf + y * mWidth + x;
        mLineEnd = mNextPixel + w;
        mLineSkip = mWidth - w;
        mWinEnd = mNextPixel + (h - 1) * mWidth + w;
        fbassert(mWinEnd <= mBufEnd);
    }
    void setPixel(Coord x, Coord y, Color color)
    {
        fbassert(x >= 0 && x <= mWidth);
        fbassert(y >= 0 && y <= mHeight);
        auto pixPtr = mBuf + y * mWidth + x;
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
        Color* pixPtr = mBuf + y * mWidth + x;
        for (int line = 0; line < h; line++) {
            auto lineEnd = pixPtr + w;
            while(pixPtr < lineEnd) {
                *(pixPtr++) = color;
            }
            pixPtr += lineSkip;
        }
    }
    void fill(Color color) {
        for (Color* ptr = mBuf; ptr < mBufEnd; ptr++) {
            *ptr = color;
        }
    }
};

#endif
