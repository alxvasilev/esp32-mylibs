/**
  Monochrome LCD Font draw layer
  @author: Alexander Vassilev
  @copyright GPL v3 License
 */
#ifndef FONT_RENDER_MONO_HPP_INCLUDED
#define FONT_RENDER_MONO_HPP_INCLUDED

#include <font.hpp>
#include <limits>
#include <lcdAssert.hpp>

template <class DisplayGfx>
class FontRenderMono: public DisplayGfx
{
protected:
    const Font* mFont = nullptr;
public:
    enum DrawFlags
    {
        kFlagNoAutoNewline = 1,
        kFlagAllowPartial = 2,
        kFlagNoBackground = 4
    };
    enum: uint32_t {
        kPutcError = 1u << 31,
        kPutcPartial = 1u << 30,
        kPutcStatusMask = 3u << 30
    };
    typedef int16_t Coord;
    const Font* font() const { return mFont; }
    uint8_t fontScale() const { return 1; }
    void setFont(const Font& font, uint8_t scale=1) { mFont = &font; }
    void setFontScale(uint8_t scale) {}
    Coord fontHeight() const { return mFont->height; }
    Coord fontWidth() const { return mFont->width; }
    int8_t charWidth(char ch=0) const { return mFont->charWidth(ch) + mFont->charSpacing; }
    int8_t lineHeight() const { return mFont->height + mFont->lineSpacing; }
    int8_t charsPerLine() const { return this->width() / charWidth(); }
    using DisplayGfx::DisplayGfx;
uint32_t putc(char ch, int flags)
{
    if (this->cursorY >= this->height()) {
        return kPutcError;
    }
    int remain = this->width() - this->cursorX;
    if (remain <= 0) {
        return 0;
    }
    uint8_t charW = ch;
    const uint8_t* sym = mFont->getCharData(charW);
    if (!sym) {
        return kPutcError;
    }
    uint8_t* wPage = this->frameBuf() + (this->cursorY >> 3) * this->width() + this->cursorX;
    const uint8_t* rPage = sym;
    int drawW;
    if (charW > remain) {
        if (flags & kFlagAllowPartial) {
            drawW = remain;
        }
        else {
            return 0;
        }
    }
    else {
        drawW = charW;
    }
    int fontHeight = this->fontHeight();
    // read a font vertical byte, mask it, and shift it, and bitwise-merge it to display memory
    int shift = (this->cursorY & 0x07);
    bool shiftLeft = true;
    int totalRows = std::min(fontHeight, this->height() - this->cursorY);
    int nRows = std::min(totalRows, 8 - shift);
    uint8_t mask = 0xff >> (8 - nRows);
    auto rend = rPage + drawW;
    auto blitFactory = this->mFgColor
        ? ((flags & kFlagNoBackground) ? &blitFuncFactory<true, true> : &blitFuncFactory<true, false>)
        : ((flags & kFlagNoBackground) ? &blitFuncFactory<false, true> : &blitFuncFactory<false, false>);
    for(;;) {
        auto blitFunc = blitFactory(shiftLeft);
        blitFunc(rPage, wPage, rend, mask, shift);
        int bitPosSrc = (nRows & 0x07);
        int availSrc = 8 - bitPosSrc;
        int bitPosDest = ((this->cursorY + nRows) & 0x07);
        int availDest = 8 - bitPosDest;
        int rowsRemain = totalRows - nRows;
        if (rowsRemain <= 0) {
            break;
        }
        if (availSrc == 8) {
            rPage += charW; // next font page
            rend = rPage + drawW;
        }
        if (availDest == 8) {
            wPage += this->width();
        }
        int avail = std::min(std::min(availSrc, availDest), rowsRemain);
        nRows += avail;
        mask = (0xff >> (8 - avail)) << bitPosSrc;
        shift = bitPosDest - bitPosSrc;
        if (shift < 0) {
            shiftLeft = false;
            shift = -shift;
        }
        else {
            shiftLeft = true;
        }
    }
    if (drawW < charW) {
        return drawW & kPutcPartial;
    }
    remain -= drawW;
    if (flags & kFlagNoBackground) {
        return (remain < mFont->charSpacing) ? -remain : drawW + mFont->charSpacing;
    }
    if (remain < mFont->charSpacing) {
        this->clear(this->cursorX + drawW, this->cursorY, remain, fontHeight);
        return -remain;
    }
    this->clear(this->cursorX + drawW, this->cursorY, mFont->charSpacing, fontHeight);
    return drawW + mFont->charSpacing;
}
template <bool Positive, bool ClearBackground>
static auto blitFuncFactory(bool shiftLeft)
{
    if (Positive) {
        return shiftLeft ? &blitPagePositive<ClearBackground, true> : &blitPagePositive<ClearBackground, false>;
    }
    else {
        return shiftLeft ? &blitPageNegative<ClearBackground, true> : &blitPageNegative<ClearBackground, false>;
    }
}
template<bool ClearBackgnd, bool ShiftLeft>
static void blitPagePositive(const uint8_t* rPage, uint8_t* wPage, const uint8_t* rend, uint8_t mask, int shift)
{
    auto rptr = rPage;
    auto wptr = wPage;
    for (; rptr < rend; rptr++, wptr++) {
        uint8_t byte = ShiftLeft ? ((*rptr & mask) << shift) : ((*rptr & mask) >> shift);
        *wptr |= byte;
        if (ClearBackgnd) {
            *wptr &= ~byte;
        }
    }
}
template<bool ClearBackgnd, bool ShiftLeft>
static void blitPageNegative(const uint8_t* rPage, uint8_t* wPage, const uint8_t* rend, uint8_t mask, int shift)
{
    auto rptr = rPage;
    auto wptr = wPage;
    for (; rptr < rend; rptr++, wptr++) {
        uint8_t byte = ShiftLeft ? ((*rptr & mask) << shift) : ((*rptr & mask) >> shift);
        *wptr &= ~byte;
        if (ClearBackgnd) {
            *wptr |= byte;
        }
    }
}
};
#endif
