#ifndef __ST7735_H__
#define __ST7735_H__

#include <stdint.h>
#include <endian.h>
#include <initializer_list>
#include "spi.hpp"
#include "lcdColor.hpp"
#include "framebuf.hpp"

class St7735Driver: public SpiMaster
{
public:
    typedef int16_t Coord;
    typedef Color565be Color;
    enum Orientation
    {
      kOrientNormal = 0,
      kOrientCW     = 1,
      kOrientCCW    = 2,
      kOrient180    = 3,
      kOrientSwapXY = 4
    };
    enum Model: uint8_t {
        kGeneric = 0,
        k1_44inch128x128 = 1,
        kGMT028_05 = 2, // 320x240 2.8 inch
        k1_9inch320x170 = 3
    };
    struct DisplayParams {
        Coord width;
        Coord height;
        Coord yoffs;
        Orientation orient;
        uint8_t madctlFlags;
        static const DisplayParams instances[];
        static const DisplayParams& get(Model model) { return instances[model]; }
    };
    struct PinCfg
    {
        SpiPinCfg spi;
        uint8_t dc; // data/command
        uint8_t rst;
    };
    enum { kMaxTransferLen = 64 };
protected:
    Coord mWidth;
    Coord mHeight;
    Coord mYoffs;
    Model mModel;
    uint8_t mRstPin;
    uint8_t mDcPin;
    void setRstLevel(int level);
    void setDcPin(int level);
    inline void execTransaction();
    void displayReset();
    static void usDelay(uint32_t us);
    static void msDelay(uint32_t ms);
    void sendCmd(uint8_t opcode);
    void sendCmd(uint8_t opcode, const std::initializer_list<uint8_t>& data);
    template <typename T>
    void sendCmd(uint8_t opcode, T data)
    {
        waitDone();
        sendCmd(opcode);
        sendData(data);
    }
    void sendData(const void* data, int size);
    void sendData(const std::initializer_list<uint8_t>& data) {
        sendData(data.begin(), (int)data.size());
    }
    template<typename T>
    void sendData(T data)
    {
        waitDone();
        setDcPin(1);
        spiSendVal(data);
    }
    void madctl(Orientation orientation, uint8_t mode);
    void setWriteWindowCoords(Coord XS, Coord YS, Coord XE, Coord YE);
    void clearBlack() { fillRect(0, 0, mWidth, mHeight, 0); }
public:
    Coord width() const { return mWidth; }
    Coord height() const { return mHeight; }
    St7735Driver(uint8_t spiHost, Model model, Coord width = -1, Coord height = -1, Coord yoffs = -1);
    void init(const PinCfg& pins);
    void prepareSendPixels();
    void sendNextPixel(Color pixel);
    void setPixel(Coord x, Coord y, Color color);
    void setWriteWindow(Coord x, Coord y, Coord w, Coord h);
    void fillRect(Coord x, Coord y, Coord w, Coord h, Color color);
    void dmaMountFrameBuffer(const FrameBuffer<Color>& fb) {
        dmaMountBuffer((const char*)fb.data(), fb.byteSize());
    }
    void dmaBlit(Coord x, Coord y, Coord w, Coord h, const char* data);
    void dmaBlit(Coord x, Coord y, const FrameBuffer<Color>& fb) {
        dmaBlit(x, y, fb.width(), fb.height(), (const char*)fb.data());
    }
    void dmaBlit(Coord x, Coord y, Coord w, Coord h, const FrameBuffer<Color>& fb) {
        fbassert(w * h * sizeof(Color) <= fb.byteSize());
        dmaBlit(x, y, w, h, (const char*)fb.data());
    }
    void dmaBlit(Coord x, Coord y, Coord w, Coord h);
};

#include "gfx.hpp"
// A simple typedef will not allow forward declarations
class ST7735Display: public Gfx<St7735Driver> { using Gfx<St7735Driver>::Gfx; };

// need the MADCTL flags defined for the DisplayParams configs
enum: uint8_t {
    ST77XX_MADCTL_RGB = 0x00, ///< Red-Green-Blue pixel order
    // ILI9341 specific
    ST77XX_MADCTL_BGR = 0x08, ///< Blue-Green-Red pixel order
    ST77XX_MADCTL_MH = 0x04,  ///< LCD refresh right to left
};
constexpr St7735Driver::DisplayParams St7735Driver::DisplayParams::instances[] = {
    // kGeneric = 0
    {.width = 320, .height = 240, .yoffs = 0, .orient = St7735Driver::kOrientSwapXY, .madctlFlags = 0},
    // k1_44inch128x128 = 1
    {.width = 128, .height = 128, .yoffs = 0, .orient = St7735Driver::kOrientCW, .madctlFlags = ST77XX_MADCTL_BGR},
    // k2_8inch320x240_GMT028_05 = 2
    {.width = 320, .height = 240, .yoffs = 0, .orient = St7735Driver::kOrientSwapXY, .madctlFlags = ST77XX_MADCTL_BGR},
    // k1_9inch320x170 = 3
    {.width = 320, .height = 170, .yoffs = 35, .orient = St7735Driver::kOrientCW, .madctlFlags = 0}
};
#endif
