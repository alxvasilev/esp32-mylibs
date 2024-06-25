#include "st7735.hpp"
#include <driver/gpio.h>
#include <esp_timer.h>
#include <driver/periph_ctrl.h>
#include <soc/spi_struct.h>
#include <esp_log.h>
#include <algorithm>
#include "stdfonts.hpp"

enum: uint8_t {
    ST77XX_NOP = 0x00,
    ST77XX_SWRESET = 0x01,
    ST77XX_RDDID = 0x04,
    ST77XX_RDDST = 0x09,
    ST77XX_SLPIN = 0x10,
    ST77XX_SLPOUT = 0x11,
    ST77XX_PTLON = 0x12,
    ST77XX_NORON = 0x13,

    ST77XX_INVOFF = 0x20,
    ST77XX_INVON = 0x21,
    ST77XX_DISPOFF = 0x28,
    ST77XX_DISPON = 0x29,
    ST77XX_CASET = 0x2A,
    ST77XX_RASET = 0x2B,
    ST77XX_RAMWR = 0x2C,
    ST77XX_RAMRD = 0x2E,

    ST77XX_PTLAR = 0x30,
    ST77XX_TEOFF = 0x34,
    ST77XX_TEON = 0x35,
    ST77XX_MADCTL = 0x36,
    ST77XX_COLMOD = 0x3A,

    ST77XX_MADCTL_MY = 0x80, ///< Bottom to top
    ST77XX_MADCTL_MX = 0x40, ///< Right to left
    ST77XX_MADCTL_MV = 0x20, ///< Reverse X and Y
    ST77XX_MADCTL_ML = 0x10, ///< LCD refresh Bottom to top
    ST77XX_MADCTL_RGB = 0x00, ///< Red-Green-Blue pixel order
    // ILI9341 specific
    ST77XX_MADCTL_BGR = 0x08, ///< Blue-Green-Red pixel order
    ST77XX_MADCTL_MH = 0x04,  ///< LCD refresh right to left

    ST77XX_RDID1 = 0xDA,
    ST77XX_RDID2 = 0xDB,
    ST77XX_RDID3 = 0xDC,
    ST77XX_RDID4 = 0xDD,

    //====
    // Some register settings
    ST7735_MADCTL_BGR = 0x08,
    ST7735_MADCTL_MH = 0x04,

    ST7735_FRMCTR1 = 0xB1,
    ST7735_FRMCTR2 = 0xB2,
    ST7735_FRMCTR3 = 0xB3,
    ST7735_INVCTR = 0xB4,
    ST7735_DISSET5 = 0xB6,

    ST7735_PWCTR1 = 0xC0,
    ST7735_PWCTR2 = 0xC1,
    ST7735_PWCTR3 = 0xC2,
    ST7735_PWCTR4 = 0xC3,
    ST7735_PWCTR5 = 0xC4,
    ST7735_VMCTR1 = 0xC5,

    ST7735_PWCTR6 = 0xFC,

    ST7735_GMCTRP1 = 0xE0,
    ST7735_GMCTRN1 = 0xE1,
};

void St7735Driver::usDelay(uint32_t us)
{
    auto end = esp_timer_get_time() + us;
    while (esp_timer_get_time() < end);
}

void St7735Driver::msDelay(uint32_t ms)
{
    usDelay(ms * 1000);
}

St7735Driver::St7735Driver(uint8_t spiHost)
:SpiMaster(spiHost)
{}

void St7735Driver::init(Coord width, Coord height, const PinCfg& pins)
{
    SpiMaster::init(pins.spi, 3);
    mWidth = width;
    mHeight = height;
    mDcPin = pins.dc;
    mRstPin = pins.rst;

    //Initialize non-SPI GPIOs
    gpio_pad_select_gpio((gpio_num_t)mDcPin);
    gpio_set_direction((gpio_num_t)mDcPin, GPIO_MODE_OUTPUT);
    gpio_pad_select_gpio((gpio_num_t)mRstPin);
    gpio_set_direction((gpio_num_t)mRstPin, GPIO_MODE_OUTPUT);

    displayReset();
}

void St7735Driver::setRstLevel(int level)
{
    gpio_set_level((gpio_num_t)mRstPin, level);
}

void St7735Driver::setDcPin(int level)
{
    gpio_set_level((gpio_num_t)mDcPin, level);
}

void St7735Driver::sendCmd(uint8_t opcode)
{
    waitDone();
    setDcPin(0);
    spiSendVal<false>(opcode);
}

void St7735Driver::prepareSendPixels()
{
    waitDone();
    setDcPin(1);
}

void St7735Driver::sendNextPixel(Color pixel)
{
    // WARNING: Requires prepareSendPixels() to have been called before
    spiSendVal(pixel.val);
}

void St7735Driver::sendCmd(uint8_t opcode, const std::initializer_list<uint8_t>& data)
{
    sendCmd(opcode);
    sendData(data);
}
void St7735Driver::sendData(const void* data, int size)
{
    waitDone();
    setDcPin(1);
    spiSend(data, size);
}

void St7735Driver::displayReset()
{
  setRstLevel(0);
  msDelay(50);
  setRstLevel(1);
  msDelay(140);
  sendCmd(ST77XX_SLPOUT);     // Sleep out, booster on
  msDelay(140);

  sendCmd(ST77XX_INVOFF);

//ST7735: sendCmd(ST77XX_MADCTL, (uint8_t)(0x08 | ST77XX_MADCTL_MX | ST77XX_MADCTL_MV))
  sendCmd(ST77XX_MADCTL, (uint8_t)(0x08 | ST77XX_MADCTL_MV));
  sendCmd(ST77XX_COLMOD, (uint8_t)0x05);

  sendCmd(ST77XX_CASET, {0x00, 0x00, 0x00, 0x7F});
  sendCmd(ST77XX_RASET, {0x00, 0x00, 0x00, 0x9F});

  sendCmd(ST77XX_NORON);   // 17: Normal display on, no args, w/delay
  clearBlack();
  sendCmd(ST77XX_DISPON); // 18: Main screen turn on, no args, delay
  msDelay(100);
//setOrientation(kOrientNormal);
}

void St7735Driver::setOrientation(Orientation orientation)
{
    sendCmd(0x36); // Memory data access control:
    switch (orientation)
    {
        case kOrientCW:
            std::swap(mWidth, mHeight);
            sendData(0xA0); // X-Y Exchange,Y-Mirror
            break;
        case kOrientCCW:
            std::swap(mWidth, mHeight);
            sendData(0x60); // X-Y Exchange,X-Mirror
            break;
        case kOrient180:
            sendData(0xc0); // X-Mirror,Y-Mirror: Bottom to top; Right to left; RGB
            break;
        default:
            sendData(0x00); // Normal: Top to Bottom; Left to Right; RGB
            break;
    }
}

void St7735Driver::setWriteWindow(Coord x, Coord y, Coord w, Coord h)
{
  setWriteWindowCoords(x, y, x + w - 1, y + h - 1);
}

void St7735Driver::setWriteWindowCoords(Coord x1, Coord y1, Coord x2, Coord y2)
{
  sendCmd(ST77XX_CASET, (uint32_t)(htobe16((uint16_t)x1) | (htobe16((uint16_t)x2) << 16)));
  sendCmd(ST77XX_RASET, (uint32_t)(htobe16((uint16_t)y1) | (htobe16((uint16_t)y2) << 16)));
  sendCmd(ST77XX_RAMWR); // Memory write
}

void St7735Driver::fillRect(Coord x, Coord y, Coord w, Coord h, Color color)
{
    setWriteWindow(x, y, w, h);
    // TODO: If we want to support SPI bus sharing, we must lock the bus
    // before modifying the fifo buffer
    waitDone();
    int num = w * h * 2;
    int bufSize = std::min(num, (int)kMaxTransferLen);
    int numWords = (bufSize + 3) / 4;
    fifoMemset(((uint32_t)color.val << 16) | color.val, numWords);
    setDcPin(1);
    do {
        int txCount = std::min(num, bufSize);
        fifoSend(txCount);
        num -= txCount;
    } while (num > 0);
}
void St7735Driver::setPixel(Coord x, Coord y, Color color)
{
    setWriteWindowCoords(x, y, x, y);
    sendData(color.val);
}
void St7735Driver::dmaBlit(Coord x, Coord y, Coord w, Coord h, const char* data, int dataLen)
{
    setWriteWindow(x, y, w, h);
    prepareSendPixels();
    dmaSend(data, dataLen);
}
void St7735Driver::dmaBlit(Coord x, Coord y, Coord w, Coord h)
{
    setWriteWindow(x, y, w, h);
    prepareSendPixels();
    dmaResend(w * h * sizeof(Color));
}
