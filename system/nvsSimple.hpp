#ifndef NVS_SIMPLE_HPP
#define NVS_SIMPLE_HPP
#include <nvs.h>
#include <esp_http_server.h>
#include "buffer.hpp"
#include <functional>

struct Substring;

namespace http {
    class Server;
}

class NvsSimple {
protected:
    nvs_handle_t mHandle = 0;
public:
    std::function<bool(const Substring& key, const Substring& val)> strValValidator;
    std::function<bool(const Substring& key, int32_t val)> int32ValValidator;
    nvs_handle_t handle() const { return mHandle; }
    operator nvs_handle() const { return mHandle; }
    esp_err_t init(bool eraseOnError);
    BufPtr<char> getString(const char* key);
    int32_t getInt32(const char* key, int32_t defVal);
    ~NvsSimple();
    static esp_err_t httpDumpToJson(httpd_req_t* req);
    static esp_err_t httpSetParam(httpd_req_t* req);
    static esp_err_t httpDelParam(httpd_req_t* req);
    void registerHttpHandlers(http::Server& server);
};

#endif
