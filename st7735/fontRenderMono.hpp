/**
  Monochrome LCD Font draw layer
  @author: Alexander Vassilev
  @copyright GPL v3 License
 */
#ifndef FONT_RENDER_MONO_HPP_INCLUDED
#define FONT_RENDER_MONO_HPP_INCLUDED

#include <font.hpp>
#include <algorithm> //for std::swap
#include <lcdAssert.hpp>

template <class DisplayGfx>
class FontRenderMono: public DisplayGfx
{
protected:
    const Font* mFont = nullptr;
    uint8_t mFontScale = 1;
public:
    enum DrawFlags
    {
        kFlagNoAutoNewline = 1,
        kFlagAllowPartial = 2
    };
    typedef int16_t Coord;
    const Font* font() const { return mFont; }
    uint8_t fontScale() const { return mFontScale; }
    void setFont(const Font& font, uint8_t scale=1) { mFont = &font; mFontScale = scale; }
    void setFontScale(uint8_t scale) { mFontScale = scale; }
    Coord fontHeight() const { return mFont->height * fontScale(); }
    Coord fontWidth() const { return mFont->width * fontScale(); }
    int8_t charWidth(char ch=0) const { return (mFont->charWidth(ch) + mFont->charSpacing) * fontScale(); }
    int8_t charHeight() const { return (mFont->height + mFont->lineSpacing) * fontScale(); }
    int8_t charsPerLine() const { return this->width() / charWidth(); }
    using DisplayGfx::DisplayGfx;
Coord putc(char ch, int flags=0)
{
    lcdassert(mFont);
    Coord xLim=10000;
    uint8_t charW = ch;
    uint8_t symPages = mFont->byteHeightOrWidth;
    uint8_t symLastPage = symPages - 1;
    const uint8_t* sym = mFont->getCharData(charW);
    uint8_t* bufDest = this->frameBuf() + (this->cursorY >> 3) * this->width() + this->cursorX;
    if (this->cursorY >= this->height())
    {
        return 0;
    }
    xLim = std::min(this->width(), xLim);
    uint8_t writeWidth;
    if (this->cursorX + charW <= xLim) {
        writeWidth = charW;
    }
    else {
        if (this->cursorX >= xLim) {
            return 0;
        }
        writeWidth = xLim - this->cursorX;
    }
    uint8_t vOfs = (this->cursorY & 0x07); //offset within the vertical byte
    if (vOfs == 0) {
        for (int page = 0; page <= symLastPage; page++) {
            uint8_t* dest = bufDest + page * this->width();
            const uint8_t* src = sym + page * charW;
            const uint8_t* srcEnd = src + writeWidth;
            uint8_t wmask = (page < symLastPage) ? 0xff : 0xff >> (8 - mFont->height); // FIXME
            for (; src < srcEnd; src++, dest++) {
                *dest = (*dest & ~wmask) | ((this->mFgColor ? (*src) : (~*src)) & wmask);
            }
        }
    }
    else
    {
        uint8_t displayFirstPageHeight = 8 - vOfs; // write height of first display page
        uint8_t firstLineFullMask = 0xff << vOfs; //masks the bits above the draw line
        bool onlyFirstLine = mFont->height <= displayFirstPageHeight;
        uint8_t wmask = onlyFirstLine //write mask
             ? ((1 << mFont->height) - 1) << vOfs //the font doesn't span to line bottom, if font height is small
             : firstLineFullMask; //font spans to bottom of line
        uint8_t* wptr = bufDest; //write pointer
        uint8_t* wend = wptr+writeWidth;
        const uint8_t* rptr = sym; //read pointer
        if (this->mFgColor) {
            while(wptr < wend) {
                uint8_t b = *wptr;
                b = (b &~ wmask) | ((*(rptr++) << vOfs) & wmask);
                *(wptr++) = b;
            }
        }
        else
        {
            while(wptr < wend) {
                uint8_t b = *wptr;
                b = (b &~ wmask) | (((~*(rptr++)) << vOfs) & wmask);
                *(wptr++) = b;
            }
        }
        if (onlyFirstLine)
        {
            return writeWidth + mFont->charSpacing; //we don't span on the next byte (page)
        }
        auto bufEnd = this->frameBuf() + this->frameBufSize();
        uint8_t wpage = 1;
        wptr = bufDest + wpage * this->width();
        wend = wptr + writeWidth;
        if (wend > bufEnd) {
            return writeWidth + mFont->charSpacing;
        }

        uint8_t secondPageMask = 0xff << displayFirstPageHeight; // low vOfs bits set
        uint8_t symLastPageHeight = mFont->height % 8;
        if (symLastPageHeight == 0) {
            symLastPageHeight = mFont->height;
        }
        uint8_t symLastPageToFirstDisplayPageMask;
        uint8_t symLastPageToSecondDisplayPageMask;
        if (symLastPageHeight >= displayFirstPageHeight) {
            symLastPageToFirstDisplayPageMask = firstLineFullMask;
            symLastPageToSecondDisplayPageMask = 0xff >> (8-(symLastPageHeight-displayFirstPageHeight));
        }
        else {
            symLastPageToFirstDisplayPageMask = firstLineFullMask & (0xff >> (8-symLastPageHeight));
            symLastPageToSecondDisplayPageMask = 0x00;
        }

        for (int8_t rpage = 0; rpage <= symLastPage; rpage++) {
            rptr = sym + rpage * charW;
            while(wptr < wend) {
                uint8_t b = *wptr;
                if (wpage > rpage) {
                    // we are drawing the bottom part of the symbol page,
                    // to the top part of the next display page.
                    uint8_t srcByte = this->mFgColor
                        ? (*(rptr++) >> displayFirstPageHeight)
                        : ((~*(rptr++)) >> displayFirstPageHeight);
                    wmask = (rpage < symLastPage)
                       ? secondPageMask // not the last symbol page, so copy all 8 bits
                       : symLastPageToSecondDisplayPageMask; //the last symbol page

                    b = (b & ~wmask) | (srcByte & wmask);
                }
                else {
                    // we are drawing the top part of the symbol page,
                    // to the bottom part of the same display page
                    wmask = (rpage < symLastPage)
                       ? firstLineFullMask // not the last symbol page, so copy all 8 bits
                       : symLastPageToFirstDisplayPageMask; //the last symbol page
                    uint8_t srcByte = this->mFgColor
                        ? ((*(rptr++) << vOfs))
                        : ((~*(rptr++) << vOfs));
                    b = (b & ~wmask) | srcByte;
                    wpage++;
                    wptr = bufDest + wpage * this->width();
                    wend = wptr + writeWidth;
                    if (wend > bufEnd)
                    {
                        return writeWidth + mFont->charSpacing;
                    }
                }
                *(wptr++) = b;
            }
        }
    }
    return writeWidth + mFont->charSpacing;
}
};
#endif
