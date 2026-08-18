#pragma once

#include "lwip/tcp.h"
#include "lwip/pbuf.h"

#include <map>
#include <string>
#include <cstdint>
#include <cstring>
#include <string_view>

enum class Method: uint8_t {
    GET  = 0,
    POST = 1
};

struct Endpoint {
    Method method;
    const char* path;

    bool operator<(const Endpoint& other) const {
        // Compare method first so that e.g. GET /echo and POST /echo are
        // distinct keys in the std::map.
        if (this->method != other.method) {
            return this->method < other.method;
        }
        return strcmp(this->path, other.path) < 0;
    }

    bool operator==(const Endpoint& other) const {
        return this->method == other.method && (strcmp(this->path, other.path) == 0);
    }
};

struct Response {
    int status;
    const char* content_type;
    const char* body;
};

typedef Response (*EndpointHandlerFunc)(std::string_view body);

class ApiServer;

// Per-connection state, heap-allocated on accept and passed to the lwIP
// callbacks via tcp_arg. Freed when the connection closes or errors out.
struct ConnectionState {
    ApiServer* server {nullptr};
    std::string rx {};          // received bytes, accumulated across TCP segments
    std::string tx {};          // response bytes queued for sending
    std::size_t tx_offset {0};  // bytes of tx already handed to tcp_write
};

class ApiServer {
public:
    ApiServer(int port = 80);
    ~ApiServer();

    void add_endpoint(const char* path, const Method method, EndpointHandlerFunc handlerPtr);
private:
    int m_port {80};
    tcp_pcb* m_server_pcb {nullptr};
    std::map<Endpoint, EndpointHandlerFunc> m_endpoints {};

    // C callbacks for lwIP
    static err_t on_accept(void* arg, tcp_pcb* new_pcb, err_t err);
    static err_t on_recv(void* arg, tcp_pcb* tpcb, pbuf* p, err_t err);
    static err_t on_sent(void* arg, tcp_pcb* tpcb, u16_t len);
    static void  on_error(void* arg, err_t err);

    err_t handle_recv(tcp_pcb* tpcb, pbuf* p, ConnectionState& state);
    void route_request(tcp_pcb* tpcb, ConnectionState& state, std::size_t body_offset);
    void send_response(tcp_pcb* tpcb, ConnectionState& state, int status, const char* content_type, const char* body);
    // Queue as much of the pending response as fits in the send buffer.
    // The rest is sent from on_sent as the client acknowledges data.
    void pump_tx(tcp_pcb* tpcb, ConnectionState& state);
    // Close the connection and free its state. Deregisters all lwIP
    // callbacks first: after tcp_close() the pcb lingers in FIN_WAIT and
    // lwIP would still invoke them with the freed state (use-after-free).
    static void close_connection(tcp_pcb* tpcb, ConnectionState* state);
};