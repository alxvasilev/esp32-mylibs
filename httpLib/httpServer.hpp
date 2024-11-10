#include <esp_http_server.h>
#include "utils.hpp"
#include <lwip/sockets.h>
namespace http {
extern const char* TAG;

class wsConnection;
class Server {
protected:
    httpd_handle_t mServer = nullptr;
    TaskHandle_t mTask = nullptr;
    void* mUserCtx = nullptr;
    uint16_t mPort = 0;
    bool mIsSsl = false;
public:
    typedef esp_err_t (*ReqHandler)(httpd_req_t *r);
    typedef void(*EsReqHandler)(httpd_req_t *r, void* userp);
    httpd_handle_t handle() const { return mServer; }
    uint16_t port() const { return mPort; }
    bool isSsl() const { return mIsSsl; }
    static TaskHandle_t getCurrentTask();
    esp_err_t start(uint16_t port, void* userCtx, int maxHandlers=20, size_t stackSize=4096);
    esp_err_t startSsl(uint16_t port, const char* cert, size_t certLen, const char* privKey,
        size_t privKeyLen, void* userCtx, int maxHandlers=20, size_t stackSize = 4096)
    {
        return ESP_ERR_NOT_SUPPORTED;
    }
    void stop();
    void on(const char* url, httpd_method_t method, ReqHandler handler, void* userp);
    void on(const char* url, httpd_method_t method, ReqHandler handler)
    {
        on(url, method, handler, mUserCtx);
    }
    template <class F>
    void queueWork(F&& aFunc)
    {
        httpd_queue_work(mServer, [](void* arg) {
            std::unique_ptr<F> func((F*)arg);
            (*func)();
        },
        new F(std::forward<F>(aFunc)));
    }
#ifdef __EXCEPTIONS
protected:
    static esp_err_t httpExcepWrapper(httpd_req_t* req);
public:
    void onEx(const char* url, httpd_method_t method, EsReqHandler handler, void* userp);
    void onEx(const char* url, httpd_method_t method, EsReqHandler handler) {
        onEx(url, method, handler, mUserCtx);
    }
#endif

#if CONFIG_HTTPD_WS_SUPPORT
    typedef esp_err_t (*wsReqHandler)(wsConnection& conn, httpd_ws_frame_t* msg);
protected:
    struct wsHandlerCtx {
        Server& server;
        wsReqHandler handler;
        void* userp;
        wsHandlerCtx(Server& aServer, wsReqHandler aHandler, void* aUserp)
            : server(aServer), handler(aHandler), userp(aUserp) {
            server.addWsHandlerCtx(this);
        }
    };
    friend class wsConnection;
    std::unique_ptr<std::vector<wsConnection*>> wsConns;
    std::unique_ptr<std::vector<std::unique_ptr<wsHandlerCtx>>> wsHandlerContexts;
    void addWsConn(wsConnection* conn) { wsConns->push_back(conn); }
    void delWsConn(wsConnection* conn) {
        for (auto it = wsConns->begin(); it != wsConns->end(); it++) {
            if (*it == conn) {
                wsConns->erase(it);
                return;
            }
        }
        ESP_LOGE(TAG, "delWsConn: Can't find connection in list");
    }
    void addWsHandlerCtx(wsHandlerCtx* ctx) {
        if (!wsHandlerContexts) {
            wsHandlerContexts.reset(new std::vector<std::unique_ptr<wsHandlerCtx>>);
        }
        wsHandlerContexts->emplace_back(ctx);
    }
    void wsOn(const char* url, httpd_method_t method, wsReqHandler handler, void* userp);
    void wsSyncBroadcast(const char* data, size_t len);
    template <class T>
    void wsAsyncBroadcast(T&& data) {
        httpd_queue_work(mServer, [](void* arg) {
            std::unique_ptr<AsyncBcastReqBase> msg((AsyncBcastReqBase*)arg);
            msg->exec();
        },
        new AsyncBcastReq<T>(*this, std::forward<T>(data)));
    }
    template <class T>
    void wsBroadcast(T&& data) {
        if (mTask && xTaskGetCurrentTaskHandle() == mTask) {
            wsSyncBroadcast(data.data(), data.size());
        } else {
            wsAsyncBroadcast(std::forward<T>(data));
        }
    }
protected:
    struct AsyncBcastReqBase {
        Server& server;
        virtual void exec() = 0;
        AsyncBcastReqBase(Server& aServer): server(aServer)
        {}
    };
    template<class T>
    struct AsyncBcastReq: public AsyncBcastReqBase {
        T msg;
        AsyncBcastReq(Server& aServer, T&& aData)
        : AsyncBcastReqBase(aServer), msg(std::forward<T>(aData)){}
        virtual void exec() override {
            server.wsSyncBroadcast(msg.data(), msg.size());
        }
    };
    static esp_err_t wsConnHandler(httpd_req_t* req);
};
class wsConnection {
protected:
    struct AsyncSendReqBase {
        wsConnection& conn;
        AsyncSendReqBase(wsConnection& aConn): conn(aConn) {}
        virtual ~AsyncSendReqBase() {}
        virtual void exec() = 0;
    };
    template <class T>
    struct AsyncSendReq: public AsyncSendReqBase {
        T dataObj;
        AsyncSendReq(wsConnection& aConn, T&& aDataObj)
        : AsyncSendReqBase(aConn), dataObj(std::forward<T>(aDataObj)) {}
        void exec() {
            conn.wsSendFrame((uint8_t*)dataObj.data(), dataObj.size());
        }
    };
public:
    Server& server;
    int fd;
    wsConnection(Server::wsHandlerCtx& ctx, httpd_req_t* req)
    : server(ctx.server), fd(httpd_req_to_sockfd(req)) {
        ESP_LOGI(TAG, "Creating ws connection");
        assert(fd > -1);
        assert(server.wsConns);
        int nodelay = 1;
        if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) < 0) {
            ESP_LOGE(TAG, "Error setting TCP_NODELAY for socket");
        }
        server.addWsConn(this);
    }
    ~wsConnection() {
        ESP_LOGI(TAG, "Deleting ws connection");
        server.delWsConn(this);
    }
    void wsSendFrame(const char* data, size_t len);
    template <class T>
    esp_err_t wsSendAsync(T&& data) {
        return httpd_queue_work(server.mServer, [](void* arg) {
            std::unique_ptr<AsyncSendReqBase> msg((AsyncSendReqBase*)arg);
            msg->exec();
        },
        new AsyncSendReq<T>(*this, std::forward<T>(data)));
    }
};

inline void Server::wsSyncBroadcast(const char* data, size_t len) {
    auto& conns = *wsConns;
    for (auto i = 0; i < conns.size(); i++) {
        conns[i]->wsSendFrame(data, len);
    }
}
#else
}; // Server class end
#endif

static esp_err_t jsonSend(httpd_req_t* req, const char* json)
{
    ESP_ERROR_CHECK(httpd_resp_set_type(req, "text/json"));
    return httpd_resp_sendstr(req, json);
}
static esp_err_t jsonSend(httpd_req_t* req, const std::string& json)
{
    ESP_ERROR_CHECK(httpd_resp_set_type(req, "text/json"));
    return httpd_resp_send(req, json.c_str(), json.size());
}
template<typename...Args>
static void jsonSendError(httpd_req_t* req, Args&&... args)
{
    static const char prefix[] = "{\"err\":\"";
    static const char suffix[] = "\"}";
    std::string json = prefix;
    (appendAny(json, std::forward<Args>(args)), ...);
    json.append(suffix);
    ESP_LOGW("HTTP", "Responding with JSON error: %.*s", json.size() - sizeof(prefix) - sizeof(suffix) + 2, json.c_str() + sizeof(prefix) - 1);
    jsonSend(req,  json);
}
esp_err_t sendEspError(httpd_req_t* req, httpd_err_code_t code, esp_err_t err, const char* msg, int msgLen = -1);

static inline void jsonSendOk(httpd_req_t* req)
{
    jsonSend(req, "{\"ret\":\"ok\"}");
}
}
