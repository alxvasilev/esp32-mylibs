#ifndef FONT_HPP
#define FONT_HPP
#include <stdint.h>
#include <assert.h>
#define _FONT_FORCE_ROM

struct Font
{
    typedef uint16_t CharCode;
    struct Range {
        CharCode firstCode;
        CharCode lastCode;
        uint16_t startIdx;
    };
    struct RangeList {
        const Range* ranges;
        uint8_t size;
        constexpr RangeList(uint8_t aSize, const Range* aRanges): ranges(aRanges), size(aSize) {}
    };
    enum Flags {
        kVertScan = 1,
        kStrippedX = 2,
        kStrippedY = 4
    };
    const uint8_t width;
    const uint8_t height;
    const CharCode firstCode;
    const CharCode lastCode;
    const uint8_t charSpacing: 4;
    const uint8_t lineSpacing: 4;
    const uint8_t* fontData;
    const uint8_t* offsets;
    const RangeList* ranges;
    const Flags flags;
    const bool isVertScan; // just for quicker access
    const uint8_t byteHeightOrWidth;
    Font(Flags aFlags, uint8_t aWidth, uint8_t aHeight, CharCode aFirst, CharCode aLast,
         const uint8_t* aData, const RangeList* aRanges, const uint8_t* aOffsets,
         uint8_t charSp, uint8_t lineSp)
    : width(aWidth), height(aHeight), firstCode(aFirst), lastCode(aLast), charSpacing(charSp), lineSpacing(lineSp),
      fontData(aData), offsets(aOffsets), ranges(aRanges), flags(aFlags), isVertScan(flags & kVertScan),
      byteHeightOrWidth(isVertScan ? ((aHeight + 7) / 8) : (aWidth + 7) / 8)
    {}
    bool isMonoSpace() const { return offsets == nullptr; }
    int codeToIdx(CharCode code) const {
        if (code < firstCode) {
            return -1;
        }
        if (code <= lastCode) {
            return code - firstCode;
        }
        if (!ranges) {
            return -1;
        }
        auto end = ranges->ranges + ranges->size;
        for (auto range = ranges->ranges; range < end; range++) {
            if (code <= range->lastCode) {
                return code >= range->firstCode ? code - range->firstCode + range->startIdx : -1;
            }
        }
        return -1;
    }
    /* Returns the char width in code, as an uint8_t */
    const uint8_t* getCharData(CharCode code, int& aWidth) const
    {
        int idx = codeToIdx(code);
        if (idx < 0) {
            return nullptr;
        }
        if (isVertScan) {
            if (!offsets) {
                aWidth = width;
                return fontData + (byteHeightOrWidth * width) * idx;
            }
            else {
                auto ofs = (idx == 0) ? 0 : offsets[idx-1];
                aWidth = (offsets[idx] - ofs) / byteHeightOrWidth;
                return fontData + ofs;
            }
        } else {
            // only monospace fonts supported
            aWidth = width;
            return fontData + (byteHeightOrWidth * height) * idx;
        }
    }
    int charWidth(CharCode ch=0) const {
        if (!offsets) {
            return width;
        }
        auto idx = codeToIdx(ch);
        if (idx < 0) {
            return 0;
        }
        auto ofs = (idx == 0) ? 0 : offsets[idx - 1];
        return (offsets[idx] - ofs) / byteHeightOrWidth;
    }
    int textMonoSpcWidth(int len) const { assert(!this->offsets); return len * (width + charSpacing); }
};

#endif // FONT_HPP
