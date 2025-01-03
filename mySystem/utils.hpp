#ifndef NETPLAYER_UTILS_HPP
#define NETPLAYER_UTILS_HPP

#include <esp_http_server.h>
#include <vector>
#include <stdarg.h>
#include <esp_log.h>
#include <freertos/semphr.h>
#include <memory>
#include <string>
#include "buffer.hpp"
#include "mutex.hpp"
#include "timer.hpp"
#include "utils-parse.hpp"
#include "tostring.hpp"

#define myassert(cond) if (!(cond)) { \
    ESP_LOGE("ASSERT", "Assertion failed: %s at %s:%d", #cond, __FILE__, __LINE__); \
    *((int*)nullptr) = 0; }

#define MY_ESP_ERRCHECK(expr, tag, msg, stmt) do {                                 \
    auto err = (expr);                                                             \
    if (err) { ESP_LOGW(tag, "Error %s: %s", msg, esp_err_to_name(err)); stmt; }   \
    } while(false)

#define TRACE ESP_LOGI("TRC", "%s:%d", __FILE__, __LINE__);

#define MY_STRINGIFY_HELPER(x) #x
#define MY_STRINGIFY(x) MY_STRINGIFY_HELPER(x)

struct utils {
protected:
    static bool sHaveSpiRam;
public:
    class Endian
    {
        static constexpr uint32_t mU32 = 0x01020304;
        static constexpr uint8_t mView = (const uint8_t&)mU32;
        Endian() = delete;
    public:
        static constexpr bool little = (mView == 0x04);
        static constexpr bool big = (mView == 0x01);
        static_assert(little || big, "Cannot determine endianness!");
    };
    static constexpr uint32_t ip4Addr(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
    {
        if (Endian::little) {
            return a | (b << 8) | (c << 16) | (d << 24);
        } else {
            return (a << 24) | (b << 16) | (c << 8) | d;
        }
    }
    static bool haveSpiRam() { return sHaveSpiRam; }
    static bool detectSpiRam();
    static void* mallocTrySpiram(size_t internalSize, size_t spiramSize)
    {
        return sHaveSpiRam ? heap_caps_malloc(spiramSize, MALLOC_CAP_SPIRAM) : malloc(internalSize);
    }
    static void* mallocTrySpiram(size_t size)
    {
        return sHaveSpiRam ? heap_caps_malloc(size, MALLOC_CAP_SPIRAM) : malloc(size);
    }
    static void* reallocTrySpiram(void* ptr, size_t size)
    {
        return isInSpiRam(ptr) ? heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM) : realloc(ptr, size);
    }
    static constexpr const uint32_t kSpiRamStartAddr = 0x3F800000;
    static bool isInSpiRam(void* addr) {
        return (uint32_t)addr >= kSpiRamStartAddr && (uint32_t)addr < (kSpiRamStartAddr + 4 * 1024 * 1024);
    }
    constexpr uint32_t static log2(uint32_t n) noexcept
    {
        return (n > 1) ? 1 + log2(n >> 1) : 0;
    }
    static int16_t currentCpuFreq();
};

class UrlParams: public KeyValParser
{
public:
    UrlParams(httpd_req_t* req);
};

class FileHandle: public std::unique_ptr<FILE, void(*)(FILE*)>
{
protected:
    typedef std::unique_ptr<FILE, void(*)(FILE*)> Base;
public:
    FileHandle(FILE* f): Base(f, [](FILE* fp) { if (fp) fclose(fp); }) {}
};

static inline TaskHandle_t currentTaskHandle()
{
    extern volatile void * volatile pxCurrentTCB;
    return (TaskHandle_t)pxCurrentTCB;
}

static inline void usDelay(uint32_t us)
{
    auto end = esp_timer_get_time() + us;
    while (esp_timer_get_time() < end);
}

static inline void msDelay(uint32_t ms)
{
    usDelay(ms * 1000);
}
static inline void msSleep(int ms)
{
    vTaskDelay(ms / portTICK_PERIOD_MS);
}

template <typename T>
auto appendAny(std::string& str, T&& val)
-> decltype(str.append(std::forward<T>(val)), std::string())
{
    return str.append(std::forward<T>(val));
}

template <typename T>
std::enable_if_t<std::is_integral_v<T> || std::is_floating_point_v<T>, std::string&>
appendAny(std::string& str, T val)
{
    char buf[32];
    toString(buf, sizeof(buf), val);
    return str.append(buf);
}

template<typename...Args>
const char* vtsnprintf(char* buf, int bufSize, Args&&... args)
{
    char* next = buf;
    auto end = buf + bufSize;
    ((next = next ? toString<kDontNullTerminate>(next, end - next, std::forward<Args>(args)) : nullptr), ...);
    if (next) {
        *next = 0;
        return next;
    } else {
        *buf = 0;
        return nullptr;
    }
}

class ElapsedTimer
{
protected:
    int64_t mTsStart;
public:
    ElapsedTimer(): mTsStart(esp_timer_get_time()) {}
    int msStartTime() const { return (mTsStart + 500 ) / 1000; }
    int64_t usStartTime() const { return mTsStart; }
    int64_t usElapsed() const { return esp_timer_get_time() - mTsStart; }
    int msElapsed() const { return (usElapsed() + 500) / 1000; }
    void reset() { mTsStart = esp_timer_get_time(); }
};

namespace std {
template<>
    struct default_delete<FILE>
    {
        void operator()(FILE* file) const { fclose(file); }
    };
}
struct MallocFreeDeleter {
    void operator()(const void* ptr) const { ::free((void*)ptr); }
};
template <typename T>
using unique_ptr_mfree = std::unique_ptr<T, MallocFreeDeleter>;

#endif
