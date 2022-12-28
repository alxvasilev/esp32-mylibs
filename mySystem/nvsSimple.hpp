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
    const char* mNamespace = nullptr; // must be a string literal, we are saving the passed pointer for later use
public:
    std::function<bool(const Substring& key, const Substring& val)> strValValidator;
    std::function<bool(const Substring& key, int32_t val)> int32ValValidator;
    nvs_handle_t handle() const { return mHandle; }
    operator nvs_handle() const { return mHandle; }
    esp_err_t init(const char* ns, bool eraseOnError);
    BufPtr<char> getString(const char* key);
    int32_t getInt32(const char* key, int32_t defVal);
    esp_err_t setString(const char* key, const char* val);
    esp_err_t setInt32(const char* key, int32_t val);
    esp_err_t commit() { return nvs_commit(mHandle); }
    void close();
    ~NvsSimple();
    static esp_err_t httpDumpToJson(httpd_req_t* req);
    static esp_err_t httpSetParam(httpd_req_t* req);
    static esp_err_t httpDelParam(httpd_req_t* req);
    void registerHttpHandlers(http::Server& server);
};

#endif
