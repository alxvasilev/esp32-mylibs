#ifndef __FONT_RENDER_COLOR_HPP
#define __FONT_RENDER_COLOR_HPP

#include <algorithm>
#include "stdfonts.hpp"
#include <string.h>

template <class GfxDisplay>
class FontRenderColor: public GfxDisplay
{
public:
    enum DrawFlags
    {
        kFlagNoAutoNewline = 1,
        kFlagAllowPartial = 2
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
int8_t charWidth(char ch=0) const { return (mFont->charWidth(ch) + mFont->charSpacing) * mFontScale; }
int8_t charHeight() const { return (mFont->height + mFont->lineSpacing) * mFontScale; }
int8_t charsPerLine() const { return this->width() / charWidth(); }
const Font* font() const { return mFont; }
void blitMonoHscan(Coord sx, Coord sy, Coord w, Coord h,
    const uint8_t* binData, int8_t bgSpacing, int scale)
{
    Coord bitW = w / scale;
    this->setWriteWindow(sx, sy, w + bgSpacing, h);
    this->prepareSendPixels();
    const uint8_t* bits = binData;
    for (int y = 0; y < h; y++) {
        uint8_t mask = 0x01;
        int rptY = 0;
        auto lineBits = bits;
        for (int x = 0; x < bitW; x++) {
            auto bit = (*bits) & mask;
            if (bit) {
                for (int rptX = 0; rptX < scale; rptX++) {
                    this->sendNextPixel(this->mFgColor);
                }
            } else {
                for (int rptX = 0; rptX < scale; rptX++) {
                    this->sendNextPixel(this->mBgColor);
                }
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
            bits = lineBits;
            continue;
        }
        if (mask != 0x01) {
            bits++;
        }
    }
}
/** @param bgSpacing Draw this number of columns with background color to the right
  */
void blitMonoVscan(Coord sx, Coord sy, Coord w, Coord h,
        const uint8_t* binData, int8_t bgSpacing, int scale)
{
    Coord endX = sx + w;
    if (endX > this->width()) {
        w = this->width() - sx - 1;
        if (w < 0) {
            return;
        }
        bgSpacing = 0;
    } else if (endX + bgSpacing > this->width()) {
        bgSpacing = this->width() - endX;
    }

    // scan horizontally in display RAM, but vertically in char data
    Coord bitH = h / scale;
    Coord bitW = w / scale;
    int8_t byteHeight = (bitH + 7) / 8;
    this->setWriteWindow(sx, sy, bitW * scale + bgSpacing, h);
    this->prepareSendPixels();
    int rptY = 0;
    uint8_t mask = 0x01;
    for (int y = 0; y < h; y++) {
        const uint8_t* bits = binData;
        for (int x = 0; x < bitW; x++) {
            auto color = ((*bits) & mask) ? this->mFgColor : this->mBgColor;
            for (int rptX = 0; rptX < scale; rptX++) {
                this->sendNextPixel(color);
            }
            bits += byteHeight;
        }
        for (int rptBg = 0; rptBg < bgSpacing; rptBg++) {
            this->sendNextPixel(this->mBgColor);
        }
        if (++rptY < scale) {
            continue;
        }
        rptY = 0;
        mask <<= 1;
        if (mask == 0) {
            mask = 0x01;
            binData++;
        }
    }
}
bool putc(uint8_t ch, uint8_t flags=0, uint8_t startCol=0)
{
    if (!mFont) {
        return false;
    }
    if (this->cursorY > this->height()) {
        return false;
    }
    uint8_t width = ch;
    // returns char width via the charcode argument
    auto charData = mFont->getCharData(width);
    if (!charData) {
        return false;
    }
    auto height = mFont->height * mFontScale;
    int charSpc = mFont->charSpacing;
    if (startCol) { // start drawing the char from the specified column
        if (startCol >= width) { // column is beyond char width
            // check if we still need to draw the spacing after that invisible char
            // needed for easier handling of scrolling text
            auto spacingToDraw = width + charSpc - startCol;
            if (spacingToDraw > charSpc) {
                return false;
            }
            spacingToDraw *= mFontScale; // but column is within char spacing
            if (this->cursorX + spacingToDraw > this->width()) {
                spacingToDraw = this->width() - this->cursorX;
                if (spacingToDraw <= 0) {
                    return false;
                }
            }
            this->clear(this->cursorX, this->cursorY, spacingToDraw, height);
            this->cursorX += spacingToDraw;
            return true;
        }
        auto byteHeight = (mFont->height + 7) / 8;
        charData += byteHeight * startCol; // skip first columns
        width -= startCol;
    }
    width *= mFontScale;
    charSpc *= mFontScale;

    // we need to calculate new cursor X in order to determine if we
    // should increment cursorY. That's why we do the newCursorX gymnastics
    Coord newCursorX = this->cursorX + width + charSpc;
    if (newCursorX > this->width()) {
        if (this->cursorX < this->width() && (flags & kFlagAllowPartial)) {
            newCursorX = this->width();
        } else {
            if (flags & kFlagNoAutoNewline) {
                return false;
            }
            this->cursorX = 0;
            this->cursorY += height + mFont->lineSpacing * mFontScale;
            newCursorX = width + charSpc;
        }
    }
    if (mFont->isVertScan) {
        blitMonoVscan(this->cursorX, this->cursorY, width, height, charData, charSpc, mFontScale);
    } else {
        blitMonoHscan(this->cursorX, this->cursorY, width, height, charData, charSpc, mFontScale);
    }
    this->cursorX = newCursorX;
    return true;
}
};
#endif
