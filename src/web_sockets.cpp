#include "web_sockets.h"

#include <esp_http_server.h>

#include "task_scheduler.h"
#include "web_server.h"

#include <mutex>
#include <deque>

extern TaskScheduler task_scheduler;
extern WebServer server;
extern EventLog logger;

static const char *TAG = "WebSockets";

static const size_t max_clients = 7;

struct ws_work_item {
    httpd_handle_t hd;
    int fd;
    char *payload;
    size_t payload_len;
    int *payload_ref_counter;

    ws_work_item(httpd_handle_t hd,
                 int fd,
                 char *payload,
                 size_t payload_len,
                 int *payload_ref_counter) :
                    hd(hd), fd(fd), payload(payload), payload_len(payload_len), payload_ref_counter(payload_ref_counter)
    {}
};

std::mutex work_queue_mutex;
std::deque<ws_work_item> work_queue;

void clear_work_item(ws_work_item *wi) {
    if (!wi)
        return;

    if (*wi->payload_ref_counter > 0) {
        --(*wi->payload_ref_counter);
    }
    if (*wi->payload_ref_counter == 0) {
        free(wi->payload);
        free(wi->payload_ref_counter);
    }
}

static void removeFd(wss_keep_alive_t h, int fd){
    wss_keep_alive_remove_client(h, fd);
    std::lock_guard<std::mutex> lock{work_queue_mutex};
    for (auto it = work_queue.begin(); it != work_queue.end(); ) {
        if (it->fd == fd) {
            clear_work_item(&(*it));
            it = work_queue.erase(it);
        } else {
            ++it;
        }
    }
}

esp_err_t wss_open_fd(wss_keep_alive_t hd, int sockfd)
{
    logger.printfln("New client connected %d", sockfd);
    return wss_keep_alive_add_client(hd, sockfd);
}

void wss_close_fd(wss_keep_alive_t hd, int sockfd)
{
    logger.printfln("Client disconnected %d", sockfd);
    //wss_keep_alive_remove_client(hd, sockfd);
    removeFd(hd, sockfd);
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        logger.printfln("Handshake done, the new connection was opened");

        int sock = httpd_req_to_sockfd(req);
        WebSockets *ws = (WebSockets*)req->user_ctx;
        wss_open_fd(ws->keep_alive, sock);

        if (ws->on_client_connect_fn) {
            ws->on_client_connect_fn(WebSocketsClient{sock, ws});
        }
        return ESP_OK;
    }
    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

    // First receive the full ws message
    /* Set max_len = 0 to get the frame len */
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        logger.printfln("httpd_ws_recv_frame failed to get frame len with %d", ret);
        return ret;
    }
    //logger.printfln("frame len is %d", ws_pkt.len);
    if (ws_pkt.len) {
        /* ws_pkt.len + 1 is for NULL termination as we are expecting a string */
        buf = (uint8_t *)calloc(1, ws_pkt.len + 1);
        if (buf == NULL) {
            logger.printfln("Failed to calloc memory for buf");
            return ESP_ERR_NO_MEM;
        }
        ws_pkt.payload = buf;
        /* Set max_len = ws_pkt.len to get the frame payload */
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            logger.printfln("httpd_ws_recv_frame failed with %d", ret);
            free(buf);
            return ret;
        }
    }

    if (ws_pkt.type == HTTPD_WS_TYPE_PONG) {
        // If it was a PONG, update the keep-alive
        //logger.printfln("Received PONG message");
        free(buf);
        WebSockets *ws = (WebSockets*)req->user_ctx;
        return wss_keep_alive_client_is_active(ws->keep_alive, httpd_req_to_sockfd(req));
    } else if (ws_pkt.type == HTTPD_WS_TYPE_TEXT) {
        // If it was a TEXT message, print it
        logger.printfln("Received packet with message: %s", ws_pkt.payload);
        /*ret = httpd_ws_send_frame(req, &ws_pkt);
        if (ret != ESP_OK) {
            logger.printfln("httpd_ws_send_frame failed with %d", ret);
        }
        logger.printfln("ws_handler: httpd_handle_t=%p, sockfd=%d, client_info:%d", req->handle,
                 httpd_req_to_sockfd(req), httpd_ws_get_fd_info(req->handle, httpd_req_to_sockfd(req)));
        free(buf);
        return ret;*/
    } else if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
        // If it was a CLOSE, remove it from the keep-alive list
        free(buf);
        WebSockets *ws = (WebSockets*)req->user_ctx;
        wss_close_fd(ws->keep_alive, httpd_req_to_sockfd(req));
        return ESP_OK;
    }
    free(buf);
    return ESP_OK;
}
/*
static void send_ping(void *arg)
{
    struct async_resp_arg *resp_arg = (struct async_resp_arg *)arg;
    httpd_handle_t hd = resp_arg->hd;
    int fd = resp_arg->fd;
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = NULL;
    ws_pkt.len = 0;
    ws_pkt.type = HTTPD_WS_TYPE_PING;

    httpd_ws_send_frame_async(hd, fd, &ws_pkt);
    free(resp_arg);
}
*/
bool client_not_alive_cb(wss_keep_alive_t h, int fd)
{
    logger.printfln("Client not alive, closing fd %d", fd);
    httpd_sess_trigger_close(wss_keep_alive_get_user_ctx(h), fd);
    wss_close_fd(h, fd);
    removeFd(h, fd);
    return true;
}

static void work(void *arg) {
    std::lock_guard<std::mutex> lock{work_queue_mutex};
    while(!work_queue.empty()) {
        ws_work_item wi = work_queue.front();
        work_queue.pop_front();

        httpd_ws_frame_t ws_pkt;
        memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

        ws_pkt.payload = (uint8_t*)wi.payload;
        ws_pkt.len = wi.payload_len;
        ws_pkt.type = wi.payload_len == 0 ? HTTPD_WS_TYPE_PING : HTTPD_WS_TYPE_TEXT;

        auto result = httpd_ws_send_frame_async(wi.hd, wi.fd, &ws_pkt);
        if(result != ESP_OK) {
            printf("failed to send frame: %d\n", result);
        }
        if (wi.payload_len == 0) {
            return;
        }
        clear_work_item(&wi);
    }
}

bool check_client_alive_cb(wss_keep_alive_t h, int fd)
{
    std::lock_guard<std::mutex> lock{work_queue_mutex};
    httpd_handle_t hd = (httpd_handle_t)wss_keep_alive_get_user_ctx(h);
    work_queue.emplace_back(hd, fd, nullptr, 0, nullptr);

    return httpd_queue_work(hd, work, nullptr) == ESP_OK;
}
/*
static void send_payload(void *arg)
{
    struct async_resp_payload_arg *resp_arg = (struct async_resp_payload_arg *)arg;

    httpd_handle_t hd = resp_arg->hd;
    int fd = resp_arg->fd;
    //printf("ws fd: %d\n", resp_arg->fd);

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

    ws_pkt.payload = (uint8_t*)resp_arg->payload;
    ws_pkt.len = resp_arg->payload_len;
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    httpd_ws_send_frame_async(hd, fd, &ws_pkt);

    if (*resp_arg->payload_ref_counter > 0) {
        --(*resp_arg->payload_ref_counter);
    }
    if (*resp_arg->payload_ref_counter == 0) {
        free(resp_arg->payload);
        free(resp_arg->payload_ref_counter);
    }
    free(resp_arg);
}
*/

void WebSocketsClient::send(const char* payload, size_t payload_len)
{
    ws->sendToClient(payload, payload_len, fd);
}

void WebSockets::sendToClient(const char *payload, size_t payload_len, int sock) {
    httpd_handle_t httpd = server.httpd;
    int *payload_ref_counter = (int*)malloc(sizeof(int));
    char *payload_copy = (char*)malloc(payload_len * sizeof(char));
    memcpy(payload_copy, payload, payload_len);

    *payload_ref_counter = 1;

    // TODO: locking here means, that we assume, the sock fd is valid up to now.
    // This holds true if sendToClient is only called from the onClientConnect callback,
    // i.e. from the same thread.
    std::lock_guard<std::mutex> lock{work_queue_mutex};
    work_queue.emplace_back(httpd, sock, payload_copy, payload_len, payload_ref_counter);

    if (httpd_queue_work(httpd, work, nullptr) != ESP_OK) {
        logger.printfln("httpd_queue_work failed!");
    }
}

void WebSockets::sendToAll(const char *payload, size_t payload_len) {
    httpd_handle_t httpd = server.httpd;
    size_t clients = 7;
    int    client_fds[7];

    std::lock_guard<std::mutex> lock{work_queue_mutex};
    auto result = httpd_get_client_list(httpd, &clients, client_fds);
    if (result != ESP_OK) {
        logger.printfln("httpd_get_client_list failed! %d", result);
        return;
    }

    int active_clients = 0;
    int http_clients = 0;
    int invalid_clients = 0;
    int unknown_clients = 0;
    //printf("payload (len: %d) after copy: %s\n", payload_len, payload_copy);

    for (size_t i=0; i < clients; ++i)
        if (httpd_ws_get_fd_info(httpd, client_fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET)
            ++active_clients;
        else if (httpd_ws_get_fd_info(httpd, client_fds[i]) == HTTPD_WS_CLIENT_HTTP)
            ++http_clients;
        else if (httpd_ws_get_fd_info(httpd, client_fds[i]) == HTTPD_WS_CLIENT_INVALID)
            ++invalid_clients;
        else
            ++unknown_clients;

    /*printf("active_clients %d, http_clients %d, invalid_clients %d, unknown_clients %d\n",
            active_clients,
            http_clients,
            invalid_clients,
            unknown_clients);*/

    if (active_clients == 0)
        return;

    int *payload_ref_counter = (int*)malloc(sizeof(int));
    *payload_ref_counter = active_clients;

    char *payload_copy = (char*)malloc(payload_len * sizeof(char));
    memcpy(payload_copy, payload, payload_len);

    for (size_t i=0; i < clients; ++i) {
        int sock = client_fds[i];
        if (httpd_ws_get_fd_info(httpd, sock) != HTTPD_WS_CLIENT_WEBSOCKET) {
            continue;
        }

        work_queue.emplace_back(httpd, sock, payload_copy, payload_len, payload_ref_counter);

        if (httpd_queue_work(httpd, work, nullptr) != ESP_OK) {
            logger.printfln("httpd_queue_work failed!");
        }
    }
}

void WebSockets::start(const char *uri)
{
    // Prepare keep-alive engine
    wss_keep_alive_config_t keep_alive_config = KEEP_ALIVE_CONFIG_DEFAULT();
    keep_alive_config.max_clients = max_clients;
    keep_alive_config.client_not_alive_cb = client_not_alive_cb;
    keep_alive_config.check_client_alive_cb = check_client_alive_cb;

    this->keep_alive = wss_keep_alive_start(&keep_alive_config);

    httpd_handle_t httpd = server.httpd;

    static const httpd_uri_t ws = {
            .uri        = uri,
            .method     = HTTP_GET,
            .handler    = ws_handler,
            .user_ctx   = this,
            .is_websocket = true,
            .handle_ws_control_frames = true
    };

    httpd_register_uri_handler(httpd, &ws);
    wss_keep_alive_set_user_ctx(keep_alive, httpd);

    //server.onConnect([this](int fd) {removeFd(this->keep_alive, fd);});
    //server.onDisconnect([this](int fd) {removeFd(this->keep_alive, fd);});
/*
    task_scheduler.scheduleWithFixedDelay("send_ws_messages", [httpd](){
        // Send async message to all connected clients that use websocket protocol every 10 seconds

    }, 10000, 10000);
*/
}

void WebSockets::onConnect(std::function<void(WebSocketsClient)> fn)
{
    on_client_connect_fn = fn;
}
