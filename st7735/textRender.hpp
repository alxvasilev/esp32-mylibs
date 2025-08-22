#ifndef __TEXT_RENDER_HPP
#define __TEXT_RENDER_HPP

#include <algorithm>
#include "stdfonts.hpp"
#include <string.h>

template <class FontRender>
class TextRender: public FontRender
{
public:
void skipCharsX(int n) { this->cursorX += textWidth(n); }
using FontRender::FontRender;
bool puts(const char* str, uint8_t flags=0)
{
    bool utf8 = !(flags & this->kFlagDisableUtf8);
    while(wchar_t ch = (uint8_t)*str) {
        if (ch == '\n') {
            newLine();
            str++;
            continue;
        }
        if (ch > 0x7f && utf8) {
            auto err = utf8ToWchar(ch, str);
            if (err) {
                return false;
            }
        }
        else {
            str++;
        }
        auto w = this->putc(ch, flags);
        if (w == 0) { // need newline
            if (flags & this->kFlagNoAutoNewline) {
                return true;
            }
            newLine(); // retry same char
            continue;
        }
        else if (!(w & this->kPutcStatusMask)) {
            this->cursorX += w;
        }
        else if (w & this->kPutcError) {
            return false;
        }
        else if (w & this->kPutcPartial) {
            this->cursorX += w & ~this->kPutcStatusMask; // partial or no room for spacing at end
            return true;
        }
    }
    return true;
}
void newLine()
{
    this->cursorX = 0;
    this->cursorY += this->lineHeight();
}
int utf8ToWchar(wchar_t& wc, const char*& utf8str)
{
    const uint8_t*& u8 = reinterpret_cast<const uint8_t*&>(utf8str);
    uint8_t b0 = *u8;
    if (b0 < 0x80) {
        wc = b0;
        if (!b0) {
            return 6;
        }
        utf8str++;
        return 0;
    }
    u8++;
    auto b1 = *u8;
    if ((b1 & 0b1100'0000) != 0b1000'0000) {
        return 1;
    }
    u8++;
    b1 &= 0b0011'1111;
    if ((b0 & 0b1110'0000) == 0b1100'0000) { // two bytes
        wc = ((b0 & 0b0001'1111) << 6) | b1;
        return 0;
    }
    auto b2 = *u8;
    if ((b2 & 0b1100'0000) != 0b1000'0000) {
        return 2;
    }
    u8++;
    b2 &= 0b0011'1111;
    if ((b0 & 0b1111'0000) == 0b1110'0000) { // three bytes
        wc = ((b0 & 0b0000'1111) << 12) | (b1 << 6) | b2;
        return 0;
    }
    if ((b0 & 0b1111'1000) != 0b1111'0000) {
        return 5;
    }
    auto b3 = *u8;
    if ((b3 & 0b1100'0000) != 0b1000'0000) {
        return 4;
    }
    u8++;
    b3 &= 0b0011'1111;
    wc = ((b0 & 0b0000'0111) << 18) | (b1 << 12) | (b2 << 6) || b3;
    return 0;
}
const char* utf8NextChar(const char* str)
{
    if (!*str) {
        return nullptr;
    }
    auto u8 = reinterpret_cast<const uint8_t*>(str);
    uint8_t ch = *(u8++);
    if (ch < 0x80) {
        return (const char*)u8;
    }
    if ((*(u8++) & 0b1100'0000) != 0b1000'0000) { // error
        return nullptr;
    }
    if ((ch & 0b1110'0000) == 0b1100'0000) { // two-byte wchar
        return (const char*)u8;
    }
    if ((*(u8++) & 0b1100'0000) != 0b1000'0000) { // error
        return nullptr;
    }
    if ((ch & 0b1111'0000) == 0b1110'0000) { // three-byte wchar
        return (const char*)u8;
    }
    if ((*(u8++) & 0b1100'0000) != 0b1000'0000) { // error
        return nullptr;
    }
    if ((ch & 0b1111'1000) == 0b1111'0000) { // 4-byte wchar
        return (const char*)u8;
    }
    return nullptr; // error
}
int utf8strlen(const char* str)
{
    int len = 0;
    while ((str = utf8NextChar(str))) {
        len++;
    }
    return len;
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
            this->putc(ch, flags);
        }
    }
}
int textWidth(const char *str, bool utf8=true) {
    return textWidth(*this->mFont, str, utf8);
}
int textMonoWidth(const Font& font, int len) {
    return font.textMonoSpcWidth(len) * this->mFontScale;
}
int textMonoWidth(int len) { return textMonoWidth(*this->mFont, len); }
int textWidth(const Font& font, const char *str, bool utf8=true)
{
    if (font.isMonoSpace()) {
        return font.textMonoSpcWidth(utf8 ? utf8strlen(str) : strlen(str)) * this->mFontScale;
    }
    else {
        int w = 0;
        if (!utf8) {
            for (const char* p = str; *p; p++) {
                w += font.charWidth(*p);
            }
        }
        else {
            wchar_t wc;
            while(str) {
                if (utf8ToWchar(wc, str)) {
                    return w;
                }
                w += font.charWidth(wc);
            }
        }
        return w * this->mFontScale;
    }
}
void putsCentered(const char *str, int reserveRight=0)
{
    int padding = (this->width() - this->cursorX - reserveRight - textWidth(str)) / 2;
    if (padding < 0) {
        padding = 0;
    }
    this->cursorX += padding;
    puts(str, this->kFlagNoAutoNewline | this->kFlagAllowPartial);
}
void gotoNextChar()
{
    this->cursorX += this->mFont->width * this->mFontScale + this->mFont->charSpacing;
}
void gotoNextLine()
{
    this->cursorY += this->mFont->height * this->mFontScale + this->mFont->lineSpacing;
}
};
#endif
