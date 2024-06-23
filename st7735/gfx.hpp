#ifndef __GFX_HPP
#define __GFX_HPP

#include <algorithm>
#include "stdfonts.hpp"
#include <string.h>

template <class Display>
class Gfx: public Display
{
public:
    typedef typename Display::Coord Coord;
    typedef typename Display::Color Color;
    enum DrawFlags
    {
        kFlagNoAutoNewline = 1,
        kFlagAllowPartial = 2
    };
protected:
    Color mBgColor = 0x0000;
    Color mFgColor = 0xffff;
    const Font* mFont = &Font_5x7;
    uint8_t mFontScale = 1;
public:
    Coord cursorX = 0;
    Coord cursorY = 0;
void setFont(const Font& font, int8_t scale=1) { mFont = &font; mFontScale = scale; }
void setFontScale(int8_t scale) { mFontScale = scale; }
Coord fontHeight() const { return mFont->height * mFontScale; }
Coord fontWidth() const { return mFont->width * mFontScale; }
int8_t charWidth(char ch=0) const { return (mFont->charWidth(ch) + mFont->charSpacing) * mFontScale; }
int8_t charHeight() const { return (mFont->height + mFont->lineSpacing) * mFontScale; }
int8_t charsPerLine() const { return Display::width() / charWidth(); }
int textWidth(int charCnt) const { return charCnt * (mFont->width + mFont->charSpacing); }
void skipCharsX(int n) { cursorX += textWidth(n); }
const Font* font() const { return mFont; }
using Display::Display;
Color fgColor() const { return mFgColor; }
Color bgColor() const { return mBgColor; }
void setFgColor(Color color) { mFgColor = color; }
void setFgColor(uint8_t r, uint8_t g, uint8_t b) { mFgColor = Color(r, g, b); }
void setBgColor(Color color) { mBgColor = color; }
void setBgColor(uint8_t r, uint8_t g, uint8_t b) { mBgColor = Color(r, g, b); }
void fillRect(Coord x, Coord y, Coord w, Coord h) { Display::fillRect(x, y, w, h, mBgColor); }
void gotoXY(Coord x, Coord y) { cursorX = x; cursorY = y; }
void clear() { Display::fillRect(0, 0, Display::width(), Display::height(), mBgColor); }
void clear(Coord x, Coord y, Coord w, Coord h) { Display::fillRect(x, y, w, h, mBgColor); }
void hLine(Coord x1, Coord x2, Coord y)
{
    fillRect(x1, y, x2 - x1 + 1, 1);
}
void vLine(Coord x, Coord y1, Coord y2)
{
    fillRect(x, y1, 1, y2 - y1 + 1);
}
void line(Coord x1, Coord y1, Coord x2, Coord y2)
{
    Coord dX = x2-x1;
    Coord dY = y2-y1;

    if (dX == 0) {
        vLine(x1, y1, y2);
        return;
    }
    if (dY == 0) {
        hLine(x1, x2, y1);
        return;
    }

    Coord dXsym = (dX > 0) ? 1 : -1;
    Coord dYsym = (dY > 0) ? 1 : -1;
    dX *= dXsym;
    dY *= dYsym;
    Coord dX2 = dX << 1;
    Coord dY2 = dY << 1;
    Coord di;

    if (dX >= dY) {
        di = dY2 - dX;
        while (x1 != x2) {
            setPixel(x1, y1, mFgColor);
            x1 += dXsym;
            if (di < 0) {
                di += dY2;
            }
            else {
                di += dY2 - dX2;
                y1 += dYsym;
            }
        }
    }
    else {
        di = dX2 - dY;
        while (y1 != y2) {
            setPixel(x1, y1, mFgColor);
            y1 += dYsym;
            if (di < 0) {
                di += dX2;
            }
            else {
                di += dX2 - dY2;
                x1 += dXsym;
            }
        }
    }
    setPixel(x1, y1, mFgColor);
}
void rect(Coord x1, Coord y1, Coord x2, Coord y2)
{
    hLine(x1, x2, y1);
    hLine(x1, x2, y2);
    vLine(x1, y1, y2);
    vLine(x2, y1, y2);
}
void blitMonoHscan(Coord sx, Coord sy, Coord w, Coord h,
    const uint8_t* binData, int8_t bgSpacing, int scale)
{
    Coord bitW = w / scale;
    Display::setWriteWindow(sx, sy, w + bgSpacing, h);
    Display::prepareSendPixels();
    const uint8_t* bits = binData;
    for (int y = 0; y < h; y++) {
        uint8_t mask = 0x01;
        int rptY = 0;
        auto lineBits = bits;
        for (int x = 0; x < bitW; x++) {
            auto bit = (*bits) & mask;
            if (bit) {
                for (int rptX = 0; rptX < scale; rptX++) {
                    Display::sendNextPixel(mFgColor);
                }
            } else {
                for (int rptX = 0; rptX < scale; rptX++) {
                    Display::sendNextPixel(mBgColor);
                }
            }
            mask <<= 1;
            if (mask == 0) {
                mask = 0x01;
                bits++;
            }
        }
        for (int i = 0; i < bgSpacing; i++) {
            Display::sendNextPixel(mBgColor);
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
    if (endX > Display::width()) {
        w = Display::width() - sx - 1;
        if (w < 0) {
            return;
        }
        bgSpacing = 0;
    } else if (endX + bgSpacing > Display::width()) {
        bgSpacing = Display::width() - endX;
    }

    // scan horizontally in display RAM, but vertically in char data
    Coord bitH = h / scale;
    Coord bitW = w / scale;
    int8_t byteHeight = (bitH + 7) / 8;
    Display::setWriteWindow(sx, sy, bitW * scale + bgSpacing, h);
    Display::prepareSendPixels();
    int rptY = 0;
    uint8_t mask = 0x01;
    for (int y = 0; y < h; y++) {
        const uint8_t* bits = binData;
        for (int x = 0; x < bitW; x++) {
            auto color = ((*bits) & mask) ? mFgColor : mBgColor;
            for (int rptX = 0; rptX < scale; rptX++) {
                Display::sendNextPixel(color);
            }
            bits += byteHeight;
        }
        for (int rptBg = 0; rptBg < bgSpacing; rptBg++) {
            Display::sendNextPixel(mBgColor);
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
    if (cursorY > Display::height()) {
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
            if (cursorX + spacingToDraw > Display::width()) {
                spacingToDraw = Display::width() - cursorX;
                if (spacingToDraw <= 0) {
                    return false;
                }
            }
            clear(cursorX, cursorY, spacingToDraw, height);
            cursorX += spacingToDraw;
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
    Coord newCursorX = cursorX + width + charSpc;
    if (newCursorX > Display::width()) {
        if (cursorX < Display::width() && (flags & kFlagAllowPartial)) {
            newCursorX = Display::width();
        } else {
            if (flags & kFlagNoAutoNewline) {
                return false;
            }
            cursorX = 0;
            cursorY += height + mFont->lineSpacing * mFontScale;
            newCursorX = width + charSpc;
        }
    }
    if (mFont->isVertScan) {
        blitMonoVscan(cursorX, cursorY, width, height, charData, charSpc, mFontScale);
    } else {
        blitMonoHscan(cursorX, cursorY, width, height, charData, charSpc, mFontScale);
    }
    cursorX = newCursorX;
    return true;
}
void puts(const char* str, uint8_t flags=0)
{
    char ch;
    while((ch = *(str++))) {
        if (ch == '\n') {
            newLine();
        } else if (ch != '\r') {
            putc(ch, flags);
        }
    }
}
void newLine()
{
    cursorX = 0;
    cursorY += (mFont->height + mFont->lineSpacing) * mFontScale;
}
void nputs(const char* str, int len, uint8_t flags=0)
{
    auto end = str + len;
    while(str < end) {
        char ch = *(str++);
        if (!ch) {
            return;
        }
        if (ch == '\n') {
            newLine();
        } else if (ch != '\r') {
            putc(ch, flags);
        }
    }
}
int textWidth(const char *str)
{
    if (mFont->isMono()) {
        return (mFont->width + mFont->charSpacing) * mFontScale * strlen(str);
    } else {
        int w = 0;
        for (const char* p = str; *p; p++) {
            w += mFontScale * mFont->charWidth(*p);
        }
        return w;
    }
}
void putsCentered(const char *str, int reserveRight=0)
{
    int padding = (Display::width() - cursorX - reserveRight - textWidth(str)) / 2;
    if (padding < 0) {
        padding = 0;
    }
    cursorX += padding;
    puts(str, kFlagNoAutoNewline | kFlagAllowPartial);
}
void gotoNextChar()
{
    cursorX += mFont->width * mFontScale + mFont->charSpacing;
}
void gotoNextLine()
{
    cursorY += mFont->height * mFontScale + mFont->lineSpacing;
}
};
#endif
