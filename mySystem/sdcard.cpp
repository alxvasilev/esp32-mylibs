#include "sdcard.hpp"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include <string.h>

static const char* TAG = "SDCARD";
bool SDCard::init(int spiPort, SDCard::PinCfg pins, const char* mountPoint)
{
    ESP_LOGI(TAG, "Initializing SD card host");
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = spiPort;
    host.flags = SDMMC_HOST_FLAG_SPI;
#if ESP_IDF_VERSION_MAJOR >= 5
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = (gpio_num_t)pins.mosi,
        .miso_io_num = (gpio_num_t)pins.miso,
        .sclk_io_num = (gpio_num_t)pins.clk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000
    };
#pragma GCC diagnostic pop
    if (spi_bus_initialize((spi_host_device_t)host.slot, &bus_cfg, SDSPI_DEFAULT_DMA) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus");
        return false;
    }
    sdspi_device_config_t slotCfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slotCfg.gpio_cs = (gpio_num_t)pins.cs;
    slotCfg.host_id = (spi_host_device_t)host.slot;
#else
    sdspi_slot_config_t slotCfg = SDSPI_SLOT_CONFIG_DEFAULT();
    slotCfg.gpio_miso = (gpio_num_t)pins.miso;
    slotCfg.gpio_mosi = (gpio_num_t)pins.mosi;
    slotCfg.gpio_sck  = (gpio_num_t)pins.clk;
    slotCfg.gpio_cs   = (gpio_num_t)pins.cs;
#endif
    esp_vfs_fat_sdmmc_mount_config_t mountCfg = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 8 * 1024,
#if ESP_IDF_VERSION_MAJOR >= 5
        .disk_status_check_enable = false,
        .use_one_fat = false
#endif
    };
    auto ret = esp_vfs_fat_sdmmc_mount(mountPoint, &host, &slotCfg, &mountCfg, &mCard);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem"); // If you want the card to be formatted, set format_if_mount_failed = true
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card: %s", esp_err_to_name(ret)); // Make sure SD card lines have pull-up resistors in place
        }
        return false;
    }
    auto size = strlen(mountPoint) + 1;
    mMountPoint.reset(new char[size]);
    memcpy(mMountPoint.get(), mountPoint, size);

    // Card has been initialized, print its properties
    ESP_LOGI(TAG, "SD Card info:");
    sdmmc_card_print_info(stdout, mCard);
    return true;
}
void SDCard::unmount() {
#if ESP_IDF_VERSION_MAJOR >= 5
    esp_vfs_fat_sdcard_unmount(mMountPoint.get(), mCard);
#else
    esp_vfs_fat_sdmmc_unmount();
#endif
    mCard = nullptr;
    mMountPoint.reset();
    ESP_LOGI(TAG, "SD Card unmounted");
}

bool SDCard::test() {
    // Use POSIX and C standard library functions to work with files.
    // First create a file.
    FILE* f = fopen("/sdcard/hello.txt", "r");
    if (f) {
        char buf[256];
        int len = fread(buf, 1, sizeof(buf)-1, f);
        buf[len] = 0;
        ESP_LOGI(TAG, "file dump: %s", buf);
        fclose(f);
    } else {
        ESP_LOGI(TAG, "Opening file for writing");
        FILE* f = fopen("/sdcard/hello.txt", "w");
        if (f == NULL) {
            ESP_LOGE(TAG, "Failed to open file for writing");
            return false;
        }
        fprintf(f, "Hello SD Card!\n");
        fclose(f);
        ESP_LOGI(TAG, "File written");
    }
    return true;
}
