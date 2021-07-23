#include "web_server.h"

#include "esp_log.h"

#include "task_scheduler.h"
#include "digest_auth.h"

#include <memory>

extern TaskScheduler task_scheduler;

void WebServer::start() {
    if (this->httpd != nullptr) {
        return;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.stack_size = 8192;
    config.max_uri_handlers = 64;
    config.global_user_ctx = this;
    /*config.task_priority = tskIDLE_PRIORITY+7;
    config.core_id = 1;*/

    // Start the httpd server
    auto result = httpd_start(&this->httpd, &config);
    if (result != ESP_OK) {
        httpd = nullptr;
        logger.printfln("Failed to start web server! %s (%d)", esp_err_to_name(result), result);
        return;
    }
}

struct UserCtx {
    WebServer *server;
    WebServerHandler *handler;
};

bool authenticate(WebServerRequest req, const char * username, const char * password) {
    String auth = req.header("Authorization");
    if(auth == "") {
        logger.printfln("auth header not found");
        return false;
    }

    if (!auth.startsWith("Digest ")) {
        logger.printfln("auth header wrong start");
        return false;
    }

    auth = auth.substring(7);
    return checkDigestAuthentication(auth.c_str(), req.methodString(), username, password, "esp32-lib", false, nullptr, nullptr, nullptr);
}

WebServerHandler* WebServer::on(const char *uri, httpd_method_t method, wshCallback callback)
{
    handlers.emplace_front(uri, method, callback, wshUploadCallback());
    WebServerHandler *result = &handlers.front();

    UserCtx *user_ctx = (UserCtx *)malloc(sizeof(UserCtx));
    user_ctx->server = this;
    user_ctx->handler = result;

    const httpd_uri_t ll_handler = {
        .uri       = uri,
        .method    = method,
        .handler   = [](httpd_req_t *req) {
            auto ctx = (UserCtx *) req->user_ctx;
            auto request = WebServerRequest{req};
            if (ctx->server->username != "" && ctx->server->password != "" && !authenticate(request, ctx->server->username.c_str(), ctx->server->password.c_str())) {
                if (ctx->server->on_not_authorized) {
                    ctx->server->on_not_authorized(request);
                    return ESP_OK;
                }
                request.requestAuthentication();
                return ESP_OK;
            }

            ctx->handler->callback(request);
            return ESP_OK;
        },
        .user_ctx  = (void*)user_ctx
    };
    httpd_register_uri_handler(httpd, &ll_handler);
    return result;
}

static const size_t SCRATCH_BUFSIZE = 4096;
static uint8_t scratch_buf[SCRATCH_BUFSIZE] = {0};

WebServerHandler* WebServer::on(const char *uri, httpd_method_t method, wshCallback callback, wshUploadCallback uploadCallback)
{
    handlers.emplace_front(uri, method, callback, uploadCallback);
    WebServerHandler *result = &handlers.front();

    UserCtx *user_ctx = (UserCtx *)malloc(sizeof(UserCtx));
    user_ctx->server = this;
    user_ctx->handler = result;

    const httpd_uri_t ll_handler = {
        .uri       = uri,
        .method    = method,
        .handler   = [](httpd_req_t *req) {
            auto ctx = (UserCtx *) req->user_ctx;
            auto request = WebServerRequest{req};
            if (ctx->server->username != "" && ctx->server->password != "" && !authenticate(request, ctx->server->username.c_str(), ctx->server->password.c_str())) {
                if (ctx->server->on_not_authorized) {
                    ctx->server->on_not_authorized(request);
                    return ESP_OK;
                }
                request.requestAuthentication();
                return ESP_OK;
            }

            size_t received = 0;
            size_t remaining = req->content_len;
            size_t index = 0;

            while (remaining > 0) {
                received = httpd_req_recv(req, (char*)scratch_buf, MIN(remaining, SCRATCH_BUFSIZE));
                // Retry if timeout occurred
                if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                    continue;
                }

                if (received <= 0) {
                    printf("File reception failed!\n");
                    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive file");
                    return ESP_FAIL;
                }

                if (received <= 0) {
                    printf("File reception failed!\n");
                    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive file");
                    return ESP_FAIL;
                }

                remaining -= received;
                if(!ctx->handler->uploadCallback(request, "not implemented", index, scratch_buf, received, remaining == 0)) {
                    return ESP_FAIL;
                }

                index += received;
            }

            ctx->handler->callback(request);
            return ESP_OK;
        },
        .user_ctx  = (void*)user_ctx
    };
    httpd_register_uri_handler(httpd, &ll_handler);
    return result;
}

void WebServer::onNotAuthorized(wshCallback callback)
{
    this->on_not_authorized = callback;
}

//From: https://developer.mozilla.org/en-US/docs/Web/HTTP/Status
const char *httpStatusCodeToString(int code) {
    switch (code) {
        case 100: return "100 Continue";
        case 101: return "101 Switching Protocols";
        case 102: return "102 Processing";
        case 103: return "103 Early Hints";
        case 200: return "200 OK";
        case 201: return "201 Created";
        case 202: return "202 Accepted";
        case 203: return "203 Non-Authoritative Information";
        case 204: return "204 No Content";
        case 205: return "205 Reset Content";
        case 206: return "206 Partial Content";
        case 207: return "207 Multi-Status";
        case 208: return "208 Already Reported";
        case 226: return "226 IM Used";
        case 300: return "300 Multiple Choices";
        case 301: return "301 Moved Permanently";
        case 302: return "302 Found";
        case 303: return "303 See Other";
        case 304: return "304 Not Modified";
        case 305: return "305 Use Proxy";
        case 306: return "306 (Unused)";
        case 307: return "307 Temporary Redirect";
        case 308: return "308 Permanent Redirect";
        case 400: return "400 Bad Request";
        case 401: return "401 Unauthorized";
        case 402: return "402 Payment Required";
        case 403: return "403 Forbidden";
        case 404: return "404 Not Found";
        case 405: return "405 Method Not Allowed";
        case 406: return "406 Not Acceptable";
        case 407: return "407 Proxy Authentication Required";
        case 408: return "408 Request Timeout";
        case 409: return "409 Conflict";
        case 410: return "410 Gone";
        case 411: return "411 Length Required";
        case 412: return "412 Precondition Failed";
        case 413: return "413 Payload Too Large";
        case 414: return "414 URI Too Long";
        case 415: return "415 Unsupported Media Type";
        case 416: return "416 Range Not Satisfiable";
        case 417: return "417 Expectation Failed";
        case 418: return "418 I'm a teapot";
        case 421: return "421 Misdirected Request";
        case 422: return "422 Unprocessable Entity";
        case 423: return "423 Locked";
        case 424: return "424 Failed Dependency";
        case 425: return "425 Too Early";
        case 426: return "426 Upgrade Required";
        case 428: return "428 Precondition Required";
        case 429: return "429 Too Many Requests";
        case 431: return "431 Request Header Fields Too Large";
        case 451: return "451 Unavailable For Legal Reasons";
        case 500: return "500 Internal Server Error";
        case 501: return "501 Not Implemented";
        case 502: return "502 Bad Gateway";
        case 503: return "503 Service Unavailable";
        case 504: return "504 Gateway Timeout";
        case 505: return "505 HTTP Version not supported";
        case 506: return "506 Variant Also Negotiates";
        case 507: return "507 Insufficient Storage";
        case 508: return "508 Loop Detected";
        case 510: return "510 Not Extended";
        case 511: return "511 Network Authentication Required";
        default:  return "";
    }
}

void WebServerRequest::send(uint16_t code, const char *content_type, const char *content, size_t content_len)
{
    auto result = httpd_resp_set_type(req, content_type);
    if (result != ESP_OK) {
        printf("Failed to set response type: %d\n", result);
        return;
    }

    result = httpd_resp_set_status(req, httpStatusCodeToString(code));
    if (result != ESP_OK) {
        printf("Failed to set response status: %d\n", result);
        return;
    }

    result = httpd_resp_send(req, content, content_len);
    if (result != ESP_OK) {
        printf("Failed to send response: %d\n", result);
        return;
    }
}

void WebServerRequest::beginChunkedResponse(uint16_t code, const char *content_type)
 {
    auto result = httpd_resp_set_type(req, content_type);
    if (result != ESP_OK) {
        printf("Failed to set response type: %d\n", result);
        return;
    }

    result = httpd_resp_set_status(req, httpStatusCodeToString(code));
    if (result != ESP_OK) {
        printf("Failed to set response status: %d\n", result);
        return;
    }
}

void WebServerRequest::sendChunk(const char *chunk, size_t chunk_len)
{
    auto result = httpd_resp_send_chunk(req, chunk, chunk_len);
    if (result != ESP_OK) {
        printf("Failed to send response chunk: %d\n", result);
        return;
    }
}

void WebServerRequest::endChunkedResponse()
 {
    auto result = httpd_resp_send_chunk(req, nullptr, 0);
    if (result != ESP_OK) {
        printf("Failed to end chunked response: %d\n", result);
        return;
    }
}

void WebServerRequest::addResponseHeader(const char *field, const char *value)
 {
    auto result = httpd_resp_set_hdr(req, field, value);
    if (result != ESP_OK) {
        printf("Failed to set response header: %d\n", result);
        return;
    }
}

void WebServerRequest::requestAuthentication() {
    String payload = "Digest ";
    payload.concat(requestDigestAuthentication("esp32-lib"));
    addResponseHeader("WWW-Authenticate", payload.c_str());
    send(401);
}

class CustomString : public String {
public:
    void setLength(int len) {
        setLen(len);
    }
};

String WebServerRequest::header(const char *header_name)
{
    auto buf_len = httpd_req_get_hdr_value_len(req, header_name) + 1;
    if (buf_len == 1)
        return String("");

    CustomString result;
    result.reserve(buf_len);
    char *buf = result.begin();
    /* Copy null terminated value string into buffer */
    if (httpd_req_get_hdr_value_str(req, header_name, buf, buf_len) != ESP_OK) {
        return String("");
    }
    result.setLength(buf_len);
    return result;
}

size_t WebServerRequest::contentLength() {
    return req->content_len;
}

char* WebServerRequest::receive()
{
    char *result = (char*)malloc(sizeof(char) * contentLength());
    httpd_req_recv(req, result, contentLength());
    return result;
}

int WebServerRequest::receive(char *buf, size_t buf_len)
{
    if (buf_len < contentLength())
        return -1;
    return httpd_req_recv(req, buf, contentLength());
}

WebServerRequest::WebServerRequest(httpd_req_t *req, bool keep_alive) : req(req) {
    if(!keep_alive)
        this->addResponseHeader("Connection", "close");
}
