#ifndef __FONT_RENDER_COLOR_HPP
#define __FONT_RENDER_COLOR_HPP

#include <algorithm>
#include "stdfonts.hpp"
#include <string.h>
#include <limits>
template <class GfxDisplay>
class FontRenderColor: public GfxDisplay
{
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
protected:
    typedef GfxDisplay::Coord Coord;
    const Font* mFont = &Font_5x7;
    uint8_t mFontScale = 1;
public:
using GfxDisplay::GfxDisplay;
void setFont(const Font& font, int8_t scale=1) { mFont = &font; mFontScale = scale; }
void setFontScale(int8_t scale) { mFontScale = scale; }
Coord fontHeight() const { return mFont->height * mFontScale; }
Coord fontWidth() const { return mFont->width * mFontScale; }
Coord charWidth(char ch=0) const { return (mFont->charWidth(ch) + mFont->charSpacing) * mFontScale; }
Coord lineHeight() const { return (mFont->height + mFont->lineSpacing) * mFontScale; }
Coord charsPerLine() const { return this->width() / charWidth(); }
const Font* font() const { return mFont; }
void blitMonoHscan(Coord w, Coord h, const uint8_t* binData, int8_t bgSpacing, int scale)
{
    auto byteWidth = (w + 7) / 8;
    auto end = binData + h * byteWidth;
    for (auto lineBits = binData; lineBits < end;) {
        auto bits = lineBits;
        uint8_t mask = 0x01;
        int rptY = 0;
        for (int x = 0; x < w; x++) {
            auto color = (*bits) & mask ? this->mFgColor : this->mBgColor;
            for (int rptX = 0; rptX < scale; rptX++) {
                this->sendNextPixel(color);
            }
            mask <<= 1;
            if (mask == 0) {
                mask = 0x01;
                bits++;
            }
        }
        for (int i = 0; i < bgSpacing; i++) {
            this->sendNextPixel(this->mBgColor);
        }
        if (++rptY < scale) {
            continue;
        }
        lineBits += byteWidth;
    }
}
/** @param bgSpacing Draw this number of columns with background color to the right
  */
void blitMonoVscan(Coord w, Coord h, const uint8_t* binData, int8_t bgSpacing, int scale)
{
    int rptY = 0;
    uint8_t mask = 0x01;
    for (int y = 0; y < h;) {
        const uint8_t* bits = binData;
        for (int x = 0; x < w; x++) {
            auto color = ((*bits) & mask) ? this->mFgColor : this->mBgColor;
            for (int rptX = 0; rptX < scale; rptX++) {
                this->sendNextPixel(color);
            }
            bits++;
        }
        for (int rptBg = 0; rptBg < bgSpacing; rptBg++) {
            this->sendNextPixel(this->mBgColor);
        }
        if (++rptY < scale) {
            continue;
        }
        y++;
        rptY = 0;
        mask <<= 1;
        if (mask == 0) {
            mask = 0x01;
            binData += w;
        }
    }
}
uint32_t putc(uint8_t ch, uint8_t flags=0, uint8_t startCol=0)
{
    if (!mFont) {
        return kPutcError;
    }
    if (this->cursorY >= this->height() || this->cursorX >= this->width()) {
        return kPutcError;
    }
    uint8_t width = ch;
    // returns char width via the charcode argument
    auto charData = mFont->getCharData(width);
    if (!charData) {
        return kPutcError;
    }
    auto scale = mFontScale;
    auto scaledH = mFont->height * scale;
    int charSpc = mFont->charSpacing;
    if (startCol) { // start drawing the char from the specified column
        if (startCol >= width) { // column is beyond char width
            // check if we still need to draw the spacing after that invisible char
            // needed for easier handling of scrolling text
            auto spacingToDraw = width + charSpc - startCol;
            if (spacingToDraw <= 0) { // FIXME: Shouldn't it be spacingToDraw <= 0
                return 0;
            }
            spacingToDraw *= scale; // but column is within char spacing
            Coord remain = this->width() - this->cursorX;
            if (spacingToDraw > remain) {
                spacingToDraw = remain;
            }
            this->clear(this->cursorX, this->cursorY, spacingToDraw, scaledH);
            return spacingToDraw;
        }
        auto byteHeight = (mFont->height + 7) / 8;
        charData += byteHeight * startCol; // skip first columns
        width -= startCol;
    }
    Coord scaledW = width * scale;
    charSpc *= scale;
    Coord endX = this->cursorX + scaledW;
    uint32_t ret;
    if (endX > this->width()) { // glyph doesn't fit
        if (!(flags & kFlagAllowPartial)) {
            return 0;
        }
        scaledW = this->width() - this->cursorX - 1;
        if (scaledW < 0) {
            return 0;
        }
        width = scaledW / scale; // adjust unscaled and unscaled when scaled changes, because of rounding
        scaledW = width * scale;
        charSpc = 0;
        ret = scaledW | kPutcPartial;
    }
    else { // glyph fits, may need to trim bg spacing
        if (endX + charSpc > this->width()) {
            charSpc = this->width() - endX;
        }
        ret = scaledW + charSpc;
    }

    // scan horizontally in display RAM, but vertically in char data
    this->setWriteWindow(this->cursorX, this->cursorY, scaledW + charSpc, scaledH);
    this->prepareSendPixels();
    if(mFont->isVertScan) {
        blitMonoVscan(width, mFont->height, charData, charSpc, mFontScale);
    }
    else {
        blitMonoHscan(width, mFont->height, charData, charSpc, mFontScale);
    }
    return ret;
}
};
#endif
