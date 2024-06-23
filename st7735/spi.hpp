#ifndef SPI_HPP
#define SPI_HPP
#include <stdint.h>
#include <soc/spi_struct.h>
#include <initializer_list>
#include <soc/spi_reg.h>
#include <esp_log.h>
#include <memory>

//#define MYSPI_DEBUG 1

struct SpiPinCfg
{
    uint8_t clk;
    uint8_t mosi;
    uint8_t cs;
};
struct DmaBufDesc {
    union {
        struct {
          uint32_t size: 12;
          uint32_t length: 12;
          uint32_t offset: 5; // h/w reserved 5bit, s/w use it as offset in buffer
          uint32_t sosf: 1;   // start of sub-frame
          uint32_t eof: 1;    // end of frame
          uint32_t owner: 1;  // hw or sw
        };
        uint32_t dw0;
    };
    uint32_t buf;       /* point to buffer data */
    uint32_t next;
};

class SpiMaster
{
protected:
    struct Deleter { void operator()(DmaBufDesc* descs) { free(descs); } };
    uint8_t mSpiHost;
    volatile spi_dev_t& mReg;
    int mDmaChan = -1;
    std::unique_ptr<DmaBufDesc, Deleter> mDmaDescs;
    int mDmaDataLen = -1;
    volatile uint32_t* fifo() const
    { return (volatile uint32_t*)((uint8_t*)(&mReg) + 0x80); }
    void configPins(const SpiPinCfg& pins);
public:
    SpiMaster(uint8_t spiHost);
    void init(const SpiPinCfg& pins, int freqDiv);
    template <bool Wait=true, typename T>
    void spiSendVal(T val)
    {
        static_assert(sizeof(val) <= sizeof(uint32_t), "");
        if (Wait) {
            waitDone();
        }
        *fifo() = val;
        fifoSend(sizeof(val));
    }
    void spiSend(const void* data, int len);
    void spiSend(const std::initializer_list<uint8_t>& data) {
        spiSend(data.begin(), (int)data.size());
    }
    void startTransaction() { mReg.cmd.usr = 1; }
    void waitDone() const { while(mReg.cmd.usr); }
    void fifoMemset(uint32_t val, int nWords=16);
    void fifoSend(int len) {
        waitDone();
        mReg.mosi_dlen.usr_mosi_dbitlen = (len << 3) - 1;
        startTransaction();
    }
    bool dmaHasBuffers() const { return mDmaDescs.get() != nullptr; }
     int dmaBufLen() const { return mDmaDataLen; }
    bool dmaEnable(int dmaChan, bool burstMode);
    void dmaMountBuffer(const char* buf, int len);
    void dmaResend(int len = -1);
    void dmaSend(const char* buf, int len); // = dmaMountBuffer + dmaSend
    void dmaUnmountBuffer();
    void dmaLogState();
};
#endif
