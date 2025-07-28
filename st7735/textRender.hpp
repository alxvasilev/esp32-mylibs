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
void puts(const char* str, uint8_t flags=0)
{
    char ch;
    while((ch = *(str++))) {
        if (ch == '\n') {
            newLine();
        } else if (ch != '\r') {
            this->putc(ch, flags);
        }
    }
}
void newLine()
{
    this->cursorX = 0;
    this->cursorY += (this->mFont->height + this->mFont->lineSpacing) * this->mFontScale;
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
int textWidth(const Font& font, int len) { return len * (font.width + font.charSpacing); }
int textWidth(int len) { return textWidth(*this->mFont, len); }
int textWidth(const char *str)
{
    if (this->mFont->isMono()) {
        return (this->mFont->width + this->mFont->charSpacing) * this->mFontScale * strlen(str);
    } else {
        int w = 0;
        for (const char* p = str; *p; p++) {
            w += this->mFontScale * this->mFont->charWidth(*p);
        }
        return w;
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
