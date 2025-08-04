#ifndef __GFX_HPP
#define __GFX_HPP

#include <algorithm>
#include "stdfonts.hpp"
#include <string.h>
#include <concepts>

template<typename T>
concept HasHline = requires(T& t) {
    { t.hLine(1, 2, 3, 0) } -> std::same_as<void>;
};
template<typename T>
concept HasVline = requires(T& t) {
    { t.vLine(1, 2, 3, 0) } -> std::same_as<void>;
};

template <class Display>
class Gfx: public Display
{
public:
    typedef typename Display::Coord Coord;
    typedef typename Display::Color Color;
protected:
    Color mBgColor = 0;
    Color mFgColor = (1 << sizeof(Color) * 8) - 1; // all bits set - must be white
public:
    Coord cursorX = 0;
    Coord cursorY = 0;
using Display::Display;
Color fgColor() const { return mFgColor; }
Color bgColor() const { return mBgColor; }
void setFgColor(Color color) { mFgColor = color; }
void setFgColor(uint8_t r, uint8_t g, uint8_t b) { mFgColor = Color(r, g, b); }
void setBgColor(Color color) { mBgColor = color; }
void setBgColor(uint8_t r, uint8_t g, uint8_t b) { mBgColor = Color(r, g, b); }
void fillRect(Coord x, Coord y, Coord w, Coord h) { Display::fillRect(x, y, w, h, mFgColor); }
void gotoXY(Coord x, Coord y) { cursorX = x; cursorY = y; }
void clear() { Display::fill(mBgColor); }
void clear(Coord x, Coord y, Coord w, Coord h) { Display::fillRect(x, y, w, h, mBgColor); }
void hLine(Coord x1, Coord x2, Coord y) requires (HasHline<Display>)
{
    Display::hLine(x1, x2, y, mFgColor);
}
void hLine(Coord x1, Coord x2, Coord y) requires (!HasHline<Display>)
{
    fillRect(x1, y, x2 - x1 + 1, 1);
}
void vLine(Coord y1, Coord y2, Coord x) requires (HasVline<Display>)
{
    Display::vLine(y1, y2, x, mFgColor);
}
void vLine(Coord x, Coord y1, Coord y2) requires (!HasVline<Display>)
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
            Display::setPixel(x1, y1, mFgColor);
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
            Display::setPixel(x1, y1, mFgColor);
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
    Display::setPixel(x1, y1, mFgColor);
}
void rect(Coord x1, Coord y1, Coord x2, Coord y2)
{
    hLine(x1, x2, y1);
    hLine(x1, x2, y2);
    vLine(x1, y1, y2);
    vLine(x2, y1, y2);
}
};
#endif
