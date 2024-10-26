#ifndef SDCARD_HPP
#define SDCARD_HPP

#include <stdint.h>
#include <memory>
#include <sd_protocol_types.h>

class SDCard
{
public:
    struct PinCfg
    {
        uint8_t clk;
        uint8_t mosi;
        uint8_t miso;
        uint8_t cs;
    };
    sdmmc_card_t* mCard = nullptr;
    SDCard(){}
    std::unique_ptr<char[]> mMountPoint;
    bool init(int port, const PinCfg pins, const char* mountPoint = "/sdcard");
    void unmount();
    bool test();
};
#endif
