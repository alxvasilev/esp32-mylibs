#ifndef MY_HTTP_CLIENT_HPP
#define MY_HTTP_CLIENT_HPP

#include <esp_http_client.h>
#include <buffer.hpp>
#include <utility>

class HttpClient {
protected:
    uint16_t mBufSize = 1024;
    uint16_t mRxTimeout = 1000;
    const char* mUserAgent;
    esp_http_client_handle_t mClient = nullptr;
    bool mConnected = false;
    volatile bool mTerminate = false;
    uint16_t mHttpStatus = 0;
public:
    static constexpr const char* kDefaultUserAgent = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/92.0.4515.159 Safari/537.36";
    static constexpr const char* kRequestErr = "Connect or request error";
    enum RetCode: int8_t { kRetError = -1, kRetAgain = 0, kRetOk = 1};
    enum { kConnectTimeout = 4000, kMaxMemResponseLen = 1024 };
    class Exception: public std::runtime_error {
        using runtime_error::runtime_error;
    };
    typedef std::pair<const char*, const char*> Header;
    typedef std::vector<Header> Headers;
    static void throwIfFail(int ret, const char* msg) {
        if (ret < 0) {
            throw Exception(msg);
        }
    }
    bool connected() const { return mConnected; }
    HttpClient(const char* ua=kDefaultUserAgent): mUserAgent(ua) {}
    ~HttpClient() { close(); }
    int64_t contentLen() const { return esp_http_client_get_content_length(mClient); }
    uint16_t httpStatus() const { return mHttpStatus; }
    int lastError() const { return esp_http_client_get_errno(mClient); }
    void checkTerminate() {
        if (mTerminate) {
            throw Exception("Aborted");
        }
    }
    static esp_err_t headerHandler(esp_http_client_event_t *evt){ return ESP_OK; }
protected:
    void create(const char* url, esp_http_client_method_t method)
    {
        assert(!mClient);
        assert(!mConnected);
        esp_http_client_config_t cfg = {};
        cfg.url = url;
        cfg.event_handler = headerHandler;
        cfg.user_data = this;
        cfg.timeout_ms = kConnectTimeout;
        cfg.buffer_size = mBufSize;
        cfg.buffer_size_tx = mBufSize;
        cfg.method = method;

        mClient = esp_http_client_init(&cfg);
        if (!mClient) {
            throw Exception("Error creating http client");
        };
        esp_http_client_set_header(mClient, "User-Agent", mUserAgent);
    }
public:
    void close()
    {
        if (mClient) {
            esp_http_client_close(mClient);
            esp_http_client_cleanup(mClient);
            mClient = NULL;
        }
        mConnected = false;
    }
    int request(const char* url, esp_http_client_method_t method, const Headers* headers=nullptr, const char* postData=nullptr, int postDataLen=0)
    {
        create(url, method);
        for (;;) {
            if (headers) {
                for (auto& hdr: *headers) {
                    esp_http_client_set_header(mClient, hdr.first, hdr.second);
                }
            }
            int ret = esp_http_client_open(mClient, postDataLen);
            if (ret != ESP_OK) {
                ESP_LOGW("http", "esp_http_client_open failed with %s", esp_err_to_name(ret));
                close();
                return ret;
            }
            esp_http_client_set_timeout_ms(mClient, mRxTimeout);
            if (postData) {
                write(postData, postDataLen);
            }
            for (;;) {
                auto clen = esp_http_client_fetch_headers(mClient);
                if (clen >= 0) {
                    break;
                }
                else if (clen == -ESP_ERR_HTTP_EAGAIN) {
                    checkTerminate();
                    continue;
                }
                else {
                    close();
                    return -ESP_ERR_HTTP_FETCH_HEADER;
                }
            }
            mHttpStatus = esp_http_client_get_status_code(mClient);
            if (mHttpStatus == 301 || mHttpStatus == 302) {
                esp_http_client_set_redirection(mClient);
                continue;
            }
            mConnected = true;
            return 1;
        }
    }
    int connectAndGet(const char* url, Headers& headers) {
        return request(url, HTTP_METHOD_GET, &headers);
    }
    int writeSome(const char* data, int dataLen)
    {
        assert(mClient && mConnected);
        int ret = esp_http_client_write(mClient, data, dataLen);
        if (ret >= 0) {
            return ret;
        }
        throw Exception("HTTP send error");
    }
    void write(const char* data, int dataLen)
    {
        assert(mClient);
        int written = 0;
        while(written < dataLen) {
            int ret = esp_http_client_write(mClient, data + written, dataLen - written);
            if (ret > 0) {
                written += ret;
            }
            else if (ret < 0) {
                throw Exception("HTTP send error");
            }
            checkTerminate();
            // (ret == 0): EAGAIN?
        };
    }
    int recvSome(char* buf, int bufSize)
    {
        assert(mClient && mConnected);
        int ret = esp_http_client_read(mClient, buf, bufSize);
        if (ret >= 0) {
            return ret;
        }
        if (ret == -ESP_ERR_HTTP_EAGAIN) {
            return -1;
        }
        else {
            throw Exception("HTTP recv error");
        }
    }
    int recv(char* buf, int bufSize)
    {
        assert(mClient && mConnected);
        int recvd = 0;
        while(recvd < bufSize) {
            int ret = esp_http_client_read(mClient, buf + recvd, bufSize - recvd);
            if (ret > 0) {
                recvd += ret;
            }
            else if (ret == -ESP_ERR_HTTP_EAGAIN) {
                checkTerminate();
                continue;
            }
            else if (ret < 0) {
                return ret;
            }
            else { // ret == 0
                return recvd;
            }
        }
        return recvd;
    }
    template<class T=DynBuffer>
    T getResponse()
    {
        assert(mClient && mConnected);
        auto clen = esp_http_client_get_content_length(mClient);
        if (clen > kMaxMemResponseLen) {
            throw Exception("Response is beyond max allowed for RAM");
        }
        T resp;
        int recvd = 0;
        bool hasClen;
        if (clen > 0) {
            hasClen = true;
            resp.reserve(clen + 1); // for terminating null, if needed
            resp.resize(clen);
        }
        else {
            hasClen = false;
            resp.resize(128);
        }
        for(;;) {
            int toRead = resp.capacity() - recvd;
            if (toRead <= 0) {
                if (hasClen) {
                    throw Exception("Actual content length is larger than declared");
                }
                int newSize = std::min((int)resp.capacity() * 3 / 2, (int)kMaxMemResponseLen);
                toRead = newSize - resp.capacity();
                if (toRead <= 0) {
                    throw Exception("Streaming response is beyond max allowed for RAM");
                }
                resp.resize(newSize);
            }
            int nrx = esp_http_client_read(mClient, (char*)resp.data() + recvd, toRead);
            if (nrx == 0) {
                if (hasClen) {
                    throw Exception("Connection closed while receiving response");
                }
                resp.resize(recvd);
                return maybeNullTerminate(std::move(resp));
            }
            else if (nrx == -ESP_ERR_HTTP_EAGAIN) { //EAGAIN
                checkTerminate();
                continue;
            }
            else if (nrx < 0) {
                throw Exception("HTTP recv error");
            }
            recvd += nrx;
            if (recvd == clen) {
                return std::move(maybeNullTerminate(std::move(resp)));
            }
        }
    }
    static DynBuffer&& maybeNullTerminate(DynBuffer&& buf) {
        buf.nullTerminate();
        return std::move(buf);
    }
    template<class T> static T&& maybeNullTerminate(T&& buf) { return std::move(buf); }
    template<class T=DynBuffer>
    T get(const char* url)
    {
        throwIfFail(request(url, HTTP_METHOD_GET), kRequestErr);
        return getResponse<T>();
    }
    template<class T=DynBuffer>
    T get(const char* url, const Headers& headers)
    {
        throwIfFail(request(url, HTTP_METHOD_GET, &headers), kRequestErr);
        return getResponse<T>();
    }
    template<class T=DynBuffer>
    T post(const char* url, const char* data, int dataLen)
    {
        throwIfFail(request(url, HTTP_METHOD_POST, nullptr, data, dataLen), kRequestErr);
        return getResponse<T>();
    }
    template <class T=DynBuffer>
    T post(const char* url, const Headers& headers, const char* data, int dataLen)
    {
        throwIfFail(request(url, HTTP_METHOD_POST, &headers, data, dataLen), kRequestErr);
        return getResponse<T>();
    }
};
template <class T=DynBuffer>
T httpPost(const char* url, const HttpClient::Headers& headers, const char* data, int dataLen)
{
    HttpClient client;
    return client.post<T>(url, headers, data, dataLen);
}
template<class T=DynBuffer>
T httpPost(const char* url, const char* data, int dataLen)
{
    HttpClient client;
    return client.post<T>(url, data, dataLen);
}
template<class T=DynBuffer>
T httpGet(const char* url, const HttpClient::Headers& headers)
{
    HttpClient client;
    return client.get<T>(url, headers);
}
template<class T=DynBuffer>
T httpGet(const char* url)
{
    HttpClient client;
    return client.get<T>(url);
}
#endif
