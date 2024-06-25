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
    struct PinCfg
    {
        SpiPinCfg spi;
        uint8_t dc; // data/command
        uint8_t rst;
    };
    enum { kMaxTransferLen = 64 };
    typedef int16_t Coord;
    typedef Color565be Color;
    enum Orientation
    {
      kOrientNormal = 0,
      kOrientCW     = 1,
      kOrientCCW    = 2,
      kOrient180    = 3
    };
protected:
    Coord mWidth;
    Coord mHeight;
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
    St7735Driver(uint8_t spiHost);
    void init(Coord width, Coord height, const PinCfg& pins);
    void prepareSendPixels();
    void sendNextPixel(Color pixel);
    void setOrientation(Orientation orientation);
    void setPixel(Coord x, Coord y, Color color);
    void setWriteWindow(Coord x, Coord y, Coord w, Coord h);
    void fillRect(Coord x, Coord y, Coord w, Coord h, Color color);
    void dmaMountFrameBuffer(const FrameBuffer<Color>& fb) {
        dmaMountBuffer((const char*)fb.data(), fb.byteSize());
    }
    void dmaBlit(Coord x, Coord y, Coord w, Coord h, const char* data, int dataLen);
    void dmaBlit(Coord x, Coord y, const FrameBuffer<Color>& fb) {
        dmaBlit(x, y, fb.width(), fb.height(), (const char*)fb.data(), fb.byteSize());
    }
    void dmaBlit(Coord x, Coord y, Coord w, Coord h, const FrameBuffer<Color>& fb) {
        dmaBlit(x, y, w, h, (const char*)fb.data(), fb.byteSize());
    }
    void dmaBlit(Coord x, Coord y, Coord w, Coord h);
};

#include "gfx.hpp"
// A simple typedef will not allow forward declarations
class ST7735Display: public Gfx<St7735Driver> { using Gfx<St7735Driver>::Gfx; };

#endif
