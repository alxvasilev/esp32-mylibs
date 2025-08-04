/**
  LCD Graphics and text layer
  @author: Alexander Vassilev
  @copyright GPL v3 License
 */
#ifndef GFX_HPP_INCLUDED
#define GFX_HPP_INCLUDED

#include <lcdAssert.hpp>

template <int Width, int Height>
class FrameBufferMono
{
public:
    typedef bool Color;
    typedef int16_t Coord;
protected:
    uint8_t mFrameBuf[Width * Height];
    template <int Max>
    void constrain(Coord& x) {
        if (x < 0) {
            x = 0;
        }
        else if (x >= Max) {
            x = Max - 1;
        }
    }
public:
    static constexpr const bool isMono = true;
    uint8_t* frameBuf() { return mFrameBuf; }
    Coord width() const { return Width; }
    Coord height() const { return Height; }
    int frameBufSize() const { return sizeof(mFrameBuf); }
    const uint8_t* frameBufEnd() const { return mFrameBuf + frameBufSize(); }
bool init()
{
    fill(false);
    return true;
}
void invertImage(void)
{
    /* Toggle all bits */
    auto end = frameBufEnd();
    for (size_t* ptr = mFrameBuf; ptr < end; ptr++) {
        *ptr = ~*ptr;
    }
}
void fill(Color color)
{
    memset(mFrameBuf, color ? 0xff : 0x00, frameBufSize());
}

typedef void (*BitOpFunc)(uint8_t* ptr, Coord w, uint8_t mask);
void bitOpRect(Coord x, Coord y, Coord w, Coord h, BitOpFunc maskFunc)
{
    constrain<Width>(x);
    constrain<Height>(y);
    if (x + w > Width) {
        w = Width - x;
    }
    if (y + h > Height) {
        h = Height - y;
    }
    int yoffs = y & 0x07;
    int currHeight = std::min(h, (Coord)(8 - yoffs));
    uint8_t mask = ((1 << currHeight) - 1) << yoffs;
    uint8_t* ptr = getPixelBytePtr(x, y);
    lcdassert(ptr + w <= frameBufEnd());
    maskFunc(ptr, w, mask);
    if(currHeight >= h) {
        return;
    }
    for(;;) {
        ptr += Width;
        lcdassert(ptr + w <= frameBufEnd());
        int thisHeight = h - currHeight;
        if (thisHeight > 8) {
            currHeight += 8;
            mask = 0xff;
            maskFunc(ptr, w, mask);
        }
        else {
            currHeight += thisHeight;
            mask = (1 << thisHeight) - 1;
            maskFunc(ptr, w, mask);
            return;
        }
    }
}
protected:
uint8_t* getPixelBytePtr(Coord x, Coord y)
{
    auto page = y >> 3; // whole bytes
    auto pixPtr = mFrameBuf + page * Width + x;
    lcdassert(pixPtr < frameBufEnd());
    return pixPtr;
}
static void clearPageBits(uint8_t* ptr, Coord w, uint8_t mask)
{
    mask = ~mask;
    auto end = ptr + w;
    for (; ptr < end; ptr++) {
        *ptr &= mask;
    }
}
static void setPageBits(uint8_t* ptr, Coord w, uint8_t mask)
{
    auto end = ptr + w;
    for (; ptr < end; ptr++) {
        *ptr |= mask;
    }
}
static void invertPageBits(uint8_t* ptr, Coord w, uint8_t mask)
{
    auto end = ptr + w;
    for (; ptr < end; ptr++) {
        *ptr = *ptr ^ mask;
    }
}
public:
void fillRect(Coord x, Coord y, Coord w, Coord h, Color color)
{
    bitOpRect(x, y, w, h, color ? &setPageBits : &clearPageBits);
}
void invertRect(Coord x, Coord y, Coord w, Coord h)
{
    bitOpRect(x, y, w, h, &invertPageBits);
}
template<bool Check=true>
void setPixel(Coord x, Coord y, Color color)
{
    if (Check) {
        if (x < 0 || x >= Width) {
            return;
        }
        if (y < 0 || y >= Height) {
            return;
        }
    }
    else {
        lcdassert(x >= 0 && x < Width);
        lcdassert(y >= 0 && y < Height);
    }
    auto pixPtr = getPixelBytePtr(x, y);
    uint8_t mask = 1 << (y & 0x07); // remainder of whole-byte division
    if (color) {
        *pixPtr |= mask;
    }
    else {
        *pixPtr &= ~mask;
    }
}
void hLine(Coord x1, Coord x2, Coord y, Color color)
{
    if (y < 0 || y >= Height) {
        return;
    }
    constrain<Width>(x1);
    constrain<Width>(x2);
    if (x2 < x1)
    {
        std::swap(x1, x2);
    }
    auto pageStart = mFrameBuf + (y >> 3) * Width;
    auto start = pageStart + x1;
    auto len = x2 - x1 + 1;
    uint8_t mask = 1 << (y & 0x07);
    if (color) {
        setPageBits(start, len, mask);
    }
    else {
        clearPageBits(start, len, mask);
    }
}
void vLine(Coord y1, Coord y2, Coord x, Color color)
{
    if (x < 0 || x >= Width) {
        return;
    }
    constrain<Height>(y1);
    constrain<Height>(y2);
    if (y1 > y2)
    {
        std::swap(y1, y2);
    }
    auto page1 = y1 >> 3;
    auto page2 = y2 >> 3;
    auto pByte1 = mFrameBuf + page1 * Width + x;
    auto pByte2 = mFrameBuf + page2 * Width + x;

    uint8_t mask1 = 0xff << (y1 & 0x07);
    uint8_t mask2 = 0xff >> (7 - (y2 & 0x07));
    if (color) {
        if (page2 == page1) // line starts and ends on same page
        {
            *pByte1 |= (mask1 & mask2);
            return;
        }
        *pByte1 |= mask1;
        *pByte2 |= mask2;
        for (auto pByte = pByte1 + Width; pByte < pByte2; pByte += Width)
        {
            *pByte = 0xff;
        }
    }
    else {
        mask1 = ~mask1;
        mask2 = ~mask2;
        if (page2 == page1) // line starts and ends on same page
        {
            *pByte1 &= (mask1 | mask2);
            return;
        }
        for (auto pByte = pByte1 + Width; pByte < pByte2; pByte += Width)
        {
            *pByte = 0x00;
        }
    }
}
};
#endif
