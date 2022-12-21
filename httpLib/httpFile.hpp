#ifndef HTTP_FILE_H
#define HTTP_FILE_H
#include <esp_http_server.h>

void httpFsRegisterHandlers(httpd_handle_t server);
esp_err_t httpGetHandler(const char* urlPath, httpd_req_t* req);
#endif
