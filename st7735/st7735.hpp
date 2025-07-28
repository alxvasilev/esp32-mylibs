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
    static constexpr bool isMono = false;
    typedef int16_t Coord;
    typedef Color565be Color;
    enum MADCTL: uint8_t {
        MADCTL_MY = 0x80, ///< Bottom to top
        MADCTL_MX = 0x40, ///< Right to left
        MADCTL_MV = 0x20, ///< Reverse X and Y
        MADCTL_ML = 0x10, ///< LCD refresh Bottom to top
        MADCTL_RGB = 0x00, ///< Red-Green-Blue pixel order
        // ILI9341 specific
        MADCTL_BGR = 0x08, ///< Blue-Green-Red pixel order
        MADCTL_MH = 0x04,  ///< LCD refresh right to left
    // orientation convenience definitions
        kOrientNormal = 0,
        kOrientCW = MADCTL_MV | MADCTL_MX,
        kOrientCCW = MADCTL_MV | MADCTL_MY,
        kOrient180 = MADCTL_MX | MADCTL_MY,
        kOrientSwapXY = MADCTL_MV
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
        uint8_t madctl;
        uint8_t clkDiv;
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
    void dmaMountFrameBuffer(const FrameBufferColor<Color>& fb) {
        dmaMountBuffer((const char*)fb.data(), fb.byteSize());
    }
    void dmaBlit(Coord x, Coord y, Coord w, Coord h, const char* data);
    void dmaBlit(Coord x, Coord y, const FrameBufferColor<Color>& fb) {
        dmaBlit(x, y, fb.width(), fb.height(), (const char*)fb.data());
    }
    void dmaBlit(Coord x, Coord y, Coord w, Coord h, const FrameBufferColor<Color>& fb) {
        fbassert(w * h * sizeof(Color) <= fb.byteSize());
        dmaBlit(x, y, w, h, (const char*)fb.data());
    }
    void dmaBlit(Coord x, Coord y, Coord w, Coord h);
};

#include "lcd.hpp"
// A simple typedef will not allow forward declarations
class ST7735Display: public Lcd<St7735Driver> {public: using Lcd<St7735Driver>::Lcd; };

constexpr St7735Driver::DisplayParams St7735Driver::DisplayParams::instances[] = {
    // kGeneric = 0
    {.width = 320, .height = 240, .yoffs = 0, .madctl = kOrientSwapXY, .clkDiv = 3},
    // k1_44inch128x128 = 1
    {.width = 128, .height = 128, .yoffs = 0, .madctl = kOrientCW | MADCTL_BGR, .clkDiv = 3},
    // k2_8inch320x240_GMT028_05 = 2
    {.width = 320, .height = 240, .yoffs = 0, .madctl = kOrientSwapXY | MADCTL_BGR, .clkDiv = 3},
    // k1_9inch320x170 = 3
    {.width = 320, .height = 170, .yoffs = 35, .madctl = kOrientCW, .clkDiv = 2}
};
#endif
