#ifndef NVS_SIMPLE_HPP
#define NVS_SIMPLE_HPP
#include <nvs.h>
#include <esp_http_server.h>
#include <utils.hpp>
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
    static constexpr const char* TAG = "NVS";
    std::function<bool(const Substring& key, const Substring& val)> strValValidator;
    std::function<bool(const Substring& key, int32_t val)> int32ValValidator;
    nvs_handle_t handle() const { return mHandle; }
    operator nvs_handle() const { return mHandle; }
    esp_err_t init(const char* ns, bool eraseOnError);
    unique_ptr_mfree<char> getString(const char* key);
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

static inline nvs_iterator_t nvsEntryNext(nvs_iterator_t it)
{
    auto err = nvs_entry_next(&it);
    if (err) {
        if (it) {
            nvs_release_iterator(it);
        }
        ESP_LOGW(NvsSimple::TAG, "getNext: nvs_entry_next returned error %s", esp_err_to_name(err));
        return nullptr;
    }
    return it;
}

static inline nvs_iterator_t nvsEntryFind(const char *partitionName, const char *nsName, nvs_type_t type)
{
    nvs_iterator_t it = nullptr;
    auto err = nvs_entry_find(partitionName, nsName, type, &it);
    if (err) {
        ESP_LOGW(NvsSimple::TAG, "nvs_entry_find returned error %s", esp_err_to_name(err));
        assert(!it);
        return nullptr;
    }
    return it;
}
#endif
