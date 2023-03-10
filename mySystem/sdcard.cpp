#include "sdcard.hpp"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"

static const char* TAG = "SDCARD";
bool SDCard::init(int spiPort, SDCard::PinCfg pins, const char* mountPoint)
{
    ESP_LOGI(TAG, "Initializing SD card host");

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = spiPort;
    host.flags = SDMMC_HOST_FLAG_SPI;
    sdspi_slot_config_t slotCfg = SDSPI_SLOT_CONFIG_DEFAULT();
    slotCfg.gpio_miso = (gpio_num_t)pins.miso;
    slotCfg.gpio_mosi = (gpio_num_t)pins.mosi;
    slotCfg.gpio_sck  = (gpio_num_t)pins.clk;
    slotCfg.gpio_cs   = (gpio_num_t)pins.cs;

    esp_vfs_fat_sdmmc_mount_config_t mountCfg = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 8 * 1024
    };
    sdmmc_card_t* card;
    auto ret = esp_vfs_fat_sdmmc_mount(mountPoint, &host, &slotCfg, &mountCfg, &card);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem"); // If you want the card to be formatted, set format_if_mount_failed = true
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card: %s", esp_err_to_name(ret)); // Make sure SD card lines have pull-up resistors in place
        }
        return false;
    }

    // Card has been initialized, print its properties
    ESP_LOGI(TAG, "SD Card info:");
    sdmmc_card_print_info(stdout, card);
    return true;
}
void SDCard::unmount() {
    esp_vfs_fat_sdmmc_unmount();
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
