#ifndef _UART_HPP_INCLUDED
#define _UART_HPP_INCLUDED
#include <driver/uart.h>
#include <esp_log.h>
#include "mutex.hpp"
#include <stdexcept>

// format: stopBits.4 parity.4 dataBits.8
constexpr static uint16_t mkDataFmt(uart_word_length_t dataBits,
    uart_parity_t parity, uart_stop_bits_t stopBits)
{
    return ((uint16_t)stopBits << 12) | ((uint16_t)parity << 8) | (uint16_t)dataBits;
}
enum: uint16_t {
    UART_8N1 = mkDataFmt(UART_DATA_8_BITS, UART_PARITY_DISABLE, UART_STOP_BITS_1),
    UART_8E1 = mkDataFmt(UART_DATA_8_BITS, UART_PARITY_EVEN, UART_STOP_BITS_1),
    UART_8D1 = mkDataFmt(UART_DATA_8_BITS, UART_PARITY_ODD, UART_STOP_BITS_1)
};

class Uart {
    int mPortNum;
    QueueHandle_t mEventQueue = 0;
public:
    Uart(int portNum): mPortNum(portNum) {}
    void config(int baudRate, uint16_t dataFmt, int rxBufSize=1024)
    {
        uart_config_t cfg = {
            .baud_rate = baudRate,
            .data_bits = static_cast<uart_word_length_t>(dataFmt & 0xff),
            .parity    = static_cast<uart_parity_t>((dataFmt >> 8) & 0x0f),
            .stop_bits = static_cast<uart_stop_bits_t>((dataFmt >> 12) & 0x0f),
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .rx_flow_ctrl_thresh = 0,
            .source_clk = UART_SCLK_APB,
        };
        ESP_LOGD("UART", "dataBits: %d, parity: %d, stopBits: %d", cfg.data_bits, cfg.parity, cfg.stop_bits);
        int intr_alloc_flags =
#if CONFIG_UART_ISR_IN_IRAM
            ESP_INTR_FLAG_IRAM;
#else
            0;
#endif
        checkError(uart_driver_install(mPortNum, rxBufSize, 0, 10, &mEventQueue, intr_alloc_flags));
        checkError(uart_param_config(mPortNum, &cfg));
    }
    QueueHandle_t eventQueue() const { return mEventQueue; }
    void checkError(esp_err_t err)
    {
        if (err != ESP_OK) {
            handleError(err);
        }
    }
    void handleError(esp_err_t err)
    {
#ifdef __EXCEPTIONS
        throw std::runtime_error(std::string("UART error: ") + esp_err_to_name(err));
#else
        ESP_ERROR_CHECK(err);
#endif
    }
    void start(int pinRx, int pinTx, int pinRts = -1, int pinCts = -1)
    {
        checkError(uart_set_pin(mPortNum, pinTx, pinRx, pinRts, pinCts));
    }
    int read(uint8_t* buf, size_t len, TickType_t timeoutTicks)
    {
        auto ret = uart_read_bytes(mPortNum, buf, len, timeoutTicks);
        if (ret < 0) {
            handleError(ESP_ERR_INVALID_ARG);
        }
        return ret;
    }
    uint8_t readByte(TickType_t timeoutTicks)
    {
        uint8_t byte;
        auto ret = uart_read_bytes(mPortNum, &byte, 1, timeoutTicks);
        if (ret == 0) {
            handleError(ESP_ERR_TIMEOUT);
        }
        else if (ret < 0) {
            handleError(ESP_ERR_INVALID_ARG);
        }
        return byte;
    }
    void write(const char* buf, size_t len)
    {
        auto ret = uart_write_bytes(mPortNum, buf, len);
        if (ret != len) {
            handleError(ret < 0 ? ESP_ERR_INVALID_ARG : ESP_ERR_INVALID_SIZE);
        }
    }
    void writeByte(char byte)
    {
        auto ret = uart_write_bytes(mPortNum, &byte, 1);
        if (ret != 1) {
            handleError(ret < 0 ? ESP_ERR_INVALID_ARG : ESP_ERR_INVALID_SIZE);
        }
    }
    int writeNonblock(const char* buf, size_t len)
    {
        auto ret = uart_tx_chars(mPortNum, buf, len);
        if (ret < 0) {
            handleError(ESP_ERR_INVALID_ARG); // doc says -1 means parameter error
        }
        return ret;
    }

    void flushInput()
    {
        checkError(uart_flush_input(mPortNum));
    }
    bool flushOutput(int msTimeout=-1)
    {
        auto ret = uart_wait_tx_done(mPortNum, msTimeout < 0 ? portMAX_DELAY : msTimeout / portTICK_PERIOD_MS);
        if (ret == ESP_OK) {
            return true;
        } else if (ret == ESP_ERR_TIMEOUT) {
            return false;
        } else {
            handleError(ret);
            return false;
        }
    }
    int inputLen()
    {
        size_t len;
        checkError(uart_get_buffered_data_len(mPortNum, &len));
        return len;
    }
};
#endif
