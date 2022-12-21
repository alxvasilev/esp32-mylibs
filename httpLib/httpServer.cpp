#include "httpServer.hpp"
namespace http {
const char* TAG = "HTTP";

void Server::wsOn(const char* url, httpd_method_t method, wsReqHandler handler, void* userp)
{
    if (!wsConns) {
        wsConns.reset(new std::vector<wsConnection*>);
    }
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    httpd_uri_t desc = {
        .uri = url,
        .method = method,
        .handler = wsConnHandler,
        .user_ctx = new wsHandlerCtx(*this, handler, userp),
        .is_websocket = 1
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(mServer, &desc));
}

esp_err_t Server::wsConnHandler(httpd_req_t* req)
{
    auto& ctx = *static_cast<wsHandlerCtx*>(req->user_ctx);
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "wsConnHandler: HTTP_GET");
        auto conn = new wsConnection(ctx, req);
        req->sess_ctx = conn;
        req->free_ctx = [](void* ctx) {
            delete static_cast<wsConnection*>(ctx);
        };
        return ctx.handler(*conn, nullptr);
    }
    auto& self = *static_cast<wsConnection*>(req->sess_ctx);
    httpd_ws_frame_t wsFrame;
    memset(&wsFrame, 0, sizeof(wsFrame));
    wsFrame.type = HTTPD_WS_TYPE_TEXT;
    /* Set max_len = 0 to get the frame len */
    esp_err_t ret = httpd_ws_recv_frame(req, &wsFrame, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_ws_recv_frame() failed to get frame len: %s", esp_err_to_name(ret));
        return ret;
    }
    std::unique_ptr<uint8_t[]> buf;
    if (wsFrame.len) {
        /* ws_pkt.len + 1 is for NULL - always null-terminate */
        buf.reset(new uint8_t[wsFrame.len + 1]);
        if (!buf) {
            ESP_LOGE(TAG, "Failed to malloc memory to receive ws frame payload");
            return ESP_ERR_NO_MEM;
        }
        wsFrame.payload = buf.get();
        /* Set max_len = ws_pkt.len to get the frame payload */
        ret = httpd_ws_recv_frame(req, &wsFrame, wsFrame.len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "httpd_ws_recv_frame failed with %s", esp_err_to_name(ret));
            return ret;
        }
        wsFrame.payload[wsFrame.len] = 0;
    }
    return ctx.handler(self, &wsFrame);
}
esp_err_t Server::start(uint16_t port, void* userCtx, int maxHandlers, size_t stackSize)
{
    if (mServer) {
        return ESP_ERR_INVALID_STATE;
    }
    mUserCtx = userCtx;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    config.stack_size = stackSize;
    config.max_uri_handlers = maxHandlers;
    config.uri_match_fn = httpd_uri_match_wildcard;
    auto err = httpd_start(&mServer, &config);
    if (err != ESP_OK) {
        return err;
    }
    mTask = xTaskGetHandle("httpd");
    return err;
}

#ifdef __EXCEPTIONS

struct HttpHandlerWrapCtx {
    httpd_uri_t desc;
    Server::EsReqHandler handler;
    void* userp;
};

esp_err_t Server::httpExcepWrapper(httpd_req_t* req)
{
    auto ctx = static_cast<HttpHandlerWrapCtx*>(req->user_ctx);
    try {
        ctx->handler(req, ctx->userp);
    } catch(std::exception& ex) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, ex.what());
        return ESP_FAIL;
    }
    return ESP_OK;
}

void Server::onEx(const char* url, httpd_method_t method, EsReqHandler handler, void* userp)
{
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    HttpHandlerWrapCtx ctx = {
        .desc = {
            .uri = url,
            .method = method,
            .handler = httpExcepWrapper,
            .is_websocket = 0
        },
        .handler = handler,
        .userp = userp
    };
    ctx.desc.user_ctx = &ctx;
    ESP_ERROR_CHECK(httpd_register_uri_handler(mServer, &ctx.desc));
}
#endif

void Server::on(const char* url, httpd_method_t method, ReqHandler handler, void* userp)
{
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    httpd_uri_t desc = {
        .uri = url,
        .method = method,
        .handler = handler,
        .user_ctx = userp,
        .is_websocket = 0
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(mServer, &desc));
}

void wsConnection::wsSendFrame(const char* data, size_t len)
{
    httpd_ws_frame_t wsPacket;
    memset(&wsPacket, 0, sizeof(httpd_ws_frame_t));
    wsPacket.payload = (uint8_t*)data;
    wsPacket.len = len;
    wsPacket.type = HTTPD_WS_TYPE_BINARY;
    auto err = httpd_ws_send_frame_async(server.mServer, fd, &wsPacket);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "wsSend: Error %s sending packet, closing ws connection", esp_err_to_name(err));
        httpd_sess_trigger_close(server.mServer, fd);
    }
}
}
