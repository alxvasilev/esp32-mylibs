#ifndef COLOR_HPP
#define COLOR_HPP

#include <stdint.h>
#include <endian.h>

struct Color565be // big-endian, as usually used by display controllers
{
    uint16_t val;
    Color565be(uint16_t aVal=0): val(aVal) {}
    Color565be(uint8_t r, uint8_t g, uint8_t b) { rgb(r, g, b); }
    void rgb(uint8_t r, uint8_t g, uint8_t b)
    {
        val = htobe16(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    }
    /*
    TODO: Fix these for big-endian
    uint8_t blue() const { return val >> 11; }
    uint8_t green() const { return (val >> 5) & 0x3f; }
    uint8_t red() const { return val & 0x1f; }
    */
    // Some ready-made 16-bit ('565') color settings:
    enum: uint16_t {
        BLACK = 0x0000,
        WHITE = 0xFFFF,
        RED = htobe16(0xF800),
        GREEN = htobe16(0x07E0),
        BLUE = htobe16(0x001F),
        CYAN = htobe16(0x07FF),
        MAGENTA = htobe16(0xF81F),
        YELLOW = htobe16(0xFFE0),
        ORANGE = htobe16(0xFC00)
    };
};
typedef Color565be Color565;

#endif // COLOR_HPP
